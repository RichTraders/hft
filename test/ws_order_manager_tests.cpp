/*
 * MIT License
 *
 * Copyright (c) 2025 NewOro Corporation
 *
 * Permission is hereby granted, free of charge, to use, copy, modify, and distribute
 * this software for any purpose with or without fee, provided that the above
 * copyright notice appears in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 */

#include <gtest/gtest.h>

#include <fstream>
#include <glaze/glaze.hpp>

#include "logger.h"
#include "websocket/order_entry/ws_order_manager.h"
#include "websocket/schema/response/order.h"

using namespace core;
using namespace trading;
using namespace common;

namespace test_utils {
std::string load_test_data(const std::string& filename) {
  std::string path = "data/execution_reports/" + filename;
  std::ifstream file(path);
  if (!file.is_open()) {
    return "";
  }
  return std::string(std::istreambuf_iterator<char>(file),
                     std::istreambuf_iterator<char>());
}
}  // namespace test_utils

class WsOrderManagerTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    logger_ = std::make_unique<Logger>();
    logger_->setLevel(LogLevel::kDebug);
    logger_->clearSink();
    producer_ = std::make_unique<Logger::Producer>(logger_->make_producer());
    order_manager_ = std::make_unique<WsOrderManager>(*producer_);
  }

  static void TearDownTestSuite() {
    order_manager_.reset();
    producer_.reset();
    logger_->shutdown();
    logger_.reset();
  }

  static std::unique_ptr<Logger> logger_;
  static std::unique_ptr<Logger::Producer> producer_;
  static std::unique_ptr<WsOrderManager> order_manager_;
};
std::unique_ptr<Logger> WsOrderManagerTest::logger_;
std::unique_ptr<Logger::Producer> WsOrderManagerTest::producer_;
std::unique_ptr<WsOrderManager> WsOrderManagerTest::order_manager_;

// ============================================================================
// Extract ClientOrderId Tests
// ============================================================================

TEST_F(WsOrderManagerTest, ExtractClientOrderId_PlaceOrder_Success) {
  std::string request_id = "orderplace_1764659499426593585";

  auto result = order_manager_->create_synthetic_execution_report(
      request_id, -2010, "Insufficient balance");

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->event.client_order_id, 1764659499426593585ULL);
}

TEST_F(WsOrderManagerTest, ExtractClientOrderId_CancelOrder_Success) {
  std::string request_id = "ordercancel_9876543210";

  auto result = order_manager_->create_synthetic_execution_report(
      request_id, -2011, "Unknown order");

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->event.client_order_id, 9876543210ULL);
}

TEST_F(WsOrderManagerTest, ExtractClientOrderId_ReplaceOrder_Success) {
  std::string request_id = "orderreplace_1234567890123456789";

  auto result = order_manager_->create_synthetic_execution_report(
      request_id, -1013, "Invalid price");

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->event.client_order_id, 1234567890123456789ULL);
}

TEST_F(WsOrderManagerTest, ExtractClientOrderId_CancelAll_Success) {
  std::string request_id = "ordercancelAll_5555555555";

  auto result = order_manager_->create_synthetic_execution_report(
      request_id, -1000, "Invalid symbol");

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->event.client_order_id, 5555555555ULL);
}

TEST_F(WsOrderManagerTest, ExtractClientOrderId_InvalidFormat_ReturnsNullopt) {
  std::string request_id = "invalid_request_id";

  // No numeric part after underscore
  auto result = order_manager_->create_synthetic_execution_report(
      request_id, -1000, "Error");

  EXPECT_FALSE(result.has_value());
}

TEST_F(WsOrderManagerTest, ExtractClientOrderId_NoUnderscore_ReturnsNullopt) {
  std::string request_id = "orderplace123456";

  auto result = order_manager_->create_synthetic_execution_report(
      request_id, -1000, "Error");

  EXPECT_FALSE(result.has_value());
}

// ============================================================================
// Register/Remove Pending Request Tests
// ============================================================================

TEST_F(WsOrderManagerTest, RegisterPendingRequest_Success) {
  PendingOrderRequest request;
  request.client_order_id = 1234567890;
  request.symbol = "BTCUSDT";
  request.side = Side::kBuy;
  request.ord_type = OrderType::kLimit;
  request.order_qty = Qty{1.5};
  request.price = Price{50000.00};
  request.time_in_force = TimeInForce::kGoodTillCancel;

  std::string request_id = "orderplace_1234567890";

  // Register pending request
  order_manager_->register_pending_request(request);

  // Create synthetic report - should have full order details
  auto result = order_manager_->create_synthetic_execution_report(
      request_id, -2010, "Insufficient balance");

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->event.symbol, "BTCUSDT");
  EXPECT_EQ(result->event.side, "BUY");
  EXPECT_EQ(result->event.order_type, "LIMIT");
  EXPECT_DOUBLE_EQ(result->event.order_price, 50000.00);
  EXPECT_DOUBLE_EQ(result->event.order_quantity, 1.5);
  EXPECT_EQ(result->event.time_in_force, "GTC");
}

TEST_F(WsOrderManagerTest, RegisterPendingRequest_MarketOrder_Success) {
  PendingOrderRequest request;
  request.client_order_id = 9999999999;
  request.symbol = "ETHUSDT";
  request.side = Side::kSell;
  request.ord_type = OrderType::kMarket;
  request.order_qty = Qty{2.0};
  request.time_in_force = TimeInForce::kImmediateOrCancel;

  std::string request_id = "orderplace_9999999999";

  order_manager_->register_pending_request(request);

  auto result = order_manager_->create_synthetic_execution_report(
      request_id, -1013, "Invalid quantity");

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->event.symbol, "ETHUSDT");
  EXPECT_EQ(result->event.side, "SELL");
  EXPECT_EQ(result->event.order_type, "MARKET");
  EXPECT_DOUBLE_EQ(result->event.order_quantity, 2.0);
  EXPECT_EQ(result->event.time_in_force, "IOC");
}

TEST_F(WsOrderManagerTest, RemovePendingRequest_Success) {
  PendingOrderRequest request;
  request.client_order_id = 7777777777;
  request.symbol = "BTCUSDT";
  request.side = Side::kBuy;
  request.ord_type = OrderType::kLimit;
  request.order_qty = Qty{1.0};
  request.price = Price{50000.00};

  std::string request_id = "orderplace_7777777777";

  order_manager_->register_pending_request(request);
  order_manager_->remove_pending_request(7777777777);

  // After removal, should create minimal execution report
  auto result = order_manager_->create_synthetic_execution_report(
      request_id, -2010, "Error");

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->event.symbol, "");
  EXPECT_EQ(result->event.side, "UNKNOWN");
  EXPECT_EQ(result->event.order_type, "UNKNOWN");
}

TEST_F(WsOrderManagerTest, RemovePendingRequest_NonExistent_NoEffect) {
  std::uint64_t request_id = 8888888888;

  // Should not crash
  order_manager_->remove_pending_request(request_id);
}

// ============================================================================
// Synthetic ExecutionReport Creation Tests
// ============================================================================

TEST_F(WsOrderManagerTest, CreateSyntheticReport_WithoutPendingRequest_MinimalData) {
  std::string request_id = "orderplace_1111111111";

  auto result = order_manager_->create_synthetic_execution_report(
      request_id, -2010, "Account has insufficient balance");

  ASSERT_TRUE(result.has_value());

  const auto& event = result->event;
  EXPECT_EQ(event.client_order_id, 1111111111ULL);
  EXPECT_EQ(event.execution_type, "REJECTED");
  EXPECT_EQ(event.order_status, "REJECTED");
  EXPECT_EQ(event.reject_reason, "Account has insufficient balance");

  // Minimal data when no pending request
  EXPECT_EQ(event.symbol, "");
  EXPECT_EQ(event.side, "UNKNOWN");
  EXPECT_EQ(event.order_type, "UNKNOWN");
  EXPECT_EQ(event.time_in_force, "UNKNOWN");
  EXPECT_DOUBLE_EQ(event.order_price, 0.0);
  EXPECT_DOUBLE_EQ(event.order_quantity, 0.0);
}

TEST_F(WsOrderManagerTest, CreateSyntheticReport_InsufficientBalance_Code2010) {
  PendingOrderRequest request;
  request.client_order_id = 2222222222;
  request.symbol = "BTCUSDT";
  request.side = Side::kBuy;
  request.ord_type = OrderType::kLimit;
  request.order_qty = Qty{10.0};
  request.price = Price{60000.00};
  request.time_in_force = TimeInForce::kGoodTillCancel;

  std::string request_id = "orderplace_2222222222";
  order_manager_->register_pending_request(request);

  auto result = order_manager_->create_synthetic_execution_report(
      request_id, -2010, "Account has insufficient balance for requested action.");

  ASSERT_TRUE(result.has_value());

  const auto& event = result->event;
  EXPECT_EQ(event.client_order_id, 2222222222ULL);
  EXPECT_EQ(event.execution_type, "REJECTED");
  EXPECT_EQ(event.order_status, "REJECTED");
  EXPECT_EQ(event.reject_reason, "Account has insufficient balance for requested action.");
  EXPECT_EQ(event.symbol, "BTCUSDT");
  EXPECT_EQ(event.side, "BUY");
  EXPECT_DOUBLE_EQ(event.order_price, 60000.00);
  EXPECT_DOUBLE_EQ(event.order_quantity, 10.0);
}

TEST_F(WsOrderManagerTest, CreateSyntheticReport_CleanupPendingRequest) {
  PendingOrderRequest request;
  request.client_order_id = 4444444444;
  request.symbol = "BTCUSDT";
  request.side = Side::kBuy;
  request.ord_type = OrderType::kLimit;
  request.order_qty = Qty{1.0};
  request.price = Price{50000.00};
  request.time_in_force = TimeInForce::kGoodTillCancel;

  std::string request_id = "orderplace_4444444444";
  order_manager_->register_pending_request(request);

  // First call should have full data
  auto result1 = order_manager_->create_synthetic_execution_report(
      request_id, -2010, "Error 1");
  ASSERT_TRUE(result1.has_value());
  EXPECT_EQ(result1->event.symbol, "BTCUSDT");

  // Second call should have minimal data (pending request cleaned up)
  auto result2 = order_manager_->create_synthetic_execution_report(
      request_id, -2010, "Error 2");
  ASSERT_TRUE(result2.has_value());
  EXPECT_EQ(result2->event.symbol, "");
}

// ============================================================================
// Real JSON Data Integration Tests
// ============================================================================

TEST_F(WsOrderManagerTest, RealJson_PlaceOrderFail_InsufficientBalance) {
  std::string json = test_utils::load_test_data("place_order_fail.json");
  ASSERT_FALSE(json.empty()) << "Failed to load place_order_fail.json";

  // Parse JSON
  schema::PlaceOrderResponse response;
  auto ec = glz::read_json(response, json);
  ASSERT_FALSE(ec) << "Failed to parse JSON: " << glz::format_error(ec, json);

  // Verify JSON structure
  EXPECT_EQ(response.id, "orderplace_1764653544496236521");
  EXPECT_EQ(response.status, 400);
  ASSERT_TRUE(response.error.has_value());
  EXPECT_EQ(response.error->code, -2010);
  EXPECT_EQ(response.error->message, "Account has insufficient balance for requested action.");

  // Register pending request for this order
  PendingOrderRequest request;
  request.client_order_id = 1764653544496236521ULL;
  request.symbol = "BTCUSDT";
  request.side = Side::kBuy;
  request.ord_type = OrderType::kLimit;
  request.order_qty = Qty{10.0};
  request.price = Price{60000.00};
  request.time_in_force = TimeInForce::kGoodTillCancel;

  order_manager_->register_pending_request(request);

  // Create synthetic execution report
  auto synthetic = order_manager_->create_synthetic_execution_report(
      response.id, response.error->code, response.error->message);

  ASSERT_TRUE(synthetic.has_value());

  const auto& event = synthetic->event;
  EXPECT_EQ(event.client_order_id, 1764653544496236521ULL);
  EXPECT_EQ(event.execution_type, "REJECTED");
  EXPECT_EQ(event.order_status, "REJECTED");
  EXPECT_EQ(event.reject_reason, "Account has insufficient balance for requested action.");
  EXPECT_EQ(event.symbol, "BTCUSDT");
  EXPECT_EQ(event.side, "BUY");
  EXPECT_EQ(event.order_type, "LIMIT");
  EXPECT_DOUBLE_EQ(event.order_price, 60000.00);
  EXPECT_DOUBLE_EQ(event.order_quantity, 10.0);
  EXPECT_EQ(event.time_in_force, "GTC");
}

TEST_F(WsOrderManagerTest, RealJson_CancelOrderFail_UnknownOrder) {
  std::string json = test_utils::load_test_data("cancel_order_response_fail.json");
  ASSERT_FALSE(json.empty()) << "Failed to load cancel_order_response_fail.json";

  // Parse JSON
  schema::CancelOrderResponse response;
  auto ec = glz::read_json(response, json);
  ASSERT_FALSE(ec) << "Failed to parse JSON: " << glz::format_error(ec, json);

  // Verify JSON structure
  EXPECT_EQ(response.id, "ordercancel_1764653550514761441");
  EXPECT_EQ(response.status, 400);
  ASSERT_TRUE(response.error.has_value());
  EXPECT_EQ(response.error->code, -2011);
  EXPECT_EQ(response.error->message, "Unknown order sent.");

  // Register pending cancel request
  PendingOrderRequest request;
  request.client_order_id = 1764653550514761441ULL;
  request.symbol = "ETHUSDT";
  request.side = Side::kSell;
  request.ord_type = OrderType::kLimit;
  request.order_qty = Qty{5.0};
  request.price = Price{3000.00};
  request.time_in_force = TimeInForce::kImmediateOrCancel;

  order_manager_->register_pending_request(request);

  // Create synthetic execution report
  auto synthetic = order_manager_->create_synthetic_execution_report(
      response.id, response.error->code, response.error->message);

  ASSERT_TRUE(synthetic.has_value());

  const auto& event = synthetic->event;
  EXPECT_EQ(event.client_order_id, 1764653550514761441ULL);
  EXPECT_EQ(event.execution_type, "REJECTED");
  EXPECT_EQ(event.order_status, "REJECTED");
  EXPECT_EQ(event.reject_reason, "Unknown order sent.");
  EXPECT_EQ(event.symbol, "ETHUSDT");
  EXPECT_EQ(event.side, "SELL");
  EXPECT_EQ(event.order_type, "LIMIT");
  EXPECT_DOUBLE_EQ(event.order_price, 3000.00);
  EXPECT_DOUBLE_EQ(event.order_quantity, 5.0);
  EXPECT_EQ(event.time_in_force, "IOC");
}

TEST_F(WsOrderManagerTest, RealJson_PlaceOrderFail_WithoutPendingRequest) {
  std::string json = test_utils::load_test_data("place_order_fail.json");
  ASSERT_FALSE(json.empty());

  schema::PlaceOrderResponse response;
  auto ec = glz::read_json(response, json);
  ASSERT_FALSE(ec);

  // Don't register pending request - simulate lost state
  auto synthetic = order_manager_->create_synthetic_execution_report(
      response.id, response.error->code, response.error->message);

  ASSERT_TRUE(synthetic.has_value());

  const auto& event = synthetic->event;
  EXPECT_EQ(event.client_order_id, 1764653544496236521ULL);
  EXPECT_EQ(event.execution_type, "REJECTED");
  EXPECT_EQ(event.order_status, "REJECTED");
  EXPECT_EQ(event.reject_reason, "Account has insufficient balance for requested action.");

  // Should have minimal data when no pending request
  EXPECT_EQ(event.symbol, "");
  EXPECT_EQ(event.side, "UNKNOWN");
  EXPECT_EQ(event.order_type, "UNKNOWN");
  EXPECT_DOUBLE_EQ(event.order_price, 0.0);
  EXPECT_DOUBLE_EQ(event.order_quantity, 0.0);
}

TEST_F(WsOrderManagerTest, RealJson_MultipleErrors_IndependentHandling) {
  // Load both error responses
  std::string place_json = test_utils::load_test_data("place_order_fail.json");
  std::string cancel_json = test_utils::load_test_data("cancel_order_response_fail.json");

  ASSERT_FALSE(place_json.empty());
  ASSERT_FALSE(cancel_json.empty());

  schema::PlaceOrderResponse place_response;
  schema::CancelOrderResponse cancel_response;

  auto ec1 = glz::read_json(place_response, place_json);
  auto ec2 = glz::read_json(cancel_response, cancel_json);

  ASSERT_FALSE(ec1);
  ASSERT_FALSE(ec2);

  // Register both requests
  PendingOrderRequest request1;
  request1.client_order_id = 1764653544496236521ULL;
  request1.symbol = "BTCUSDT";
  request1.side = Side::kBuy;
  request1.ord_type = OrderType::kLimit;
  request1.order_qty = Qty{1.0};
  request1.price = Price{50000.00};
  request1.time_in_force = TimeInForce::kGoodTillCancel;

  PendingOrderRequest request2;
  request2.client_order_id = 1764653550514761441ULL;
  request2.symbol = "ETHUSDT";
  request2.side = Side::kSell;
  request2.ord_type = OrderType::kMarket;
  request2.order_qty = Qty{2.0};

  order_manager_->register_pending_request(request1);
  order_manager_->register_pending_request(request2);

  // Process first error
  auto synthetic1 = order_manager_->create_synthetic_execution_report(
      place_response.id, place_response.error->code, place_response.error->message);

  ASSERT_TRUE(synthetic1.has_value());
  EXPECT_EQ(synthetic1->event.client_order_id, 1764653544496236521ULL);
  EXPECT_EQ(synthetic1->event.symbol, "BTCUSDT");

  // Process second error
  auto synthetic2 = order_manager_->create_synthetic_execution_report(
      cancel_response.id, cancel_response.error->code, cancel_response.error->message);

  ASSERT_TRUE(synthetic2.has_value());
  EXPECT_EQ(synthetic2->event.client_order_id, 1764653550514761441ULL);
  EXPECT_EQ(synthetic2->event.symbol, "ETHUSDT");

  // Both should be independent
  EXPECT_NE(synthetic1->event.client_order_id, synthetic2->event.client_order_id);
}
