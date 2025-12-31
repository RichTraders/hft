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

#include "logger.h"
#include "websocket/order_entry/ws_order_manager.hpp"
#include "websocket/order_entry/exchanges/binance/spot/binance_spot_oe_traits.h"
#include "websocket/schema/spot/response/order.h"

using namespace core;
using namespace trading;
using namespace common;
using PriceType = common::PriceType;
using QtyType = common::QtyType;

// Type alias for the test
using TestWsOrderManager = WsOrderManager<BinanceSpotOeTraits>;

namespace test_utils {
std::string load_test_data(const std::string& filename) {
  std::string path = "data/binance_spot/json/execution_reports/" + filename;
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
    order_manager_ = std::make_unique<TestWsOrderManager>(*producer_);
  }

  static void TearDownTestSuite() {
    order_manager_.reset();
    producer_.reset();
    logger_->shutdown();
    logger_.reset();
  }

  static std::unique_ptr<Logger> logger_;
  static std::unique_ptr<Logger::Producer> producer_;
  static std::unique_ptr<TestWsOrderManager> order_manager_;
};
std::unique_ptr<Logger> WsOrderManagerTest::logger_;
std::unique_ptr<Logger::Producer> WsOrderManagerTest::producer_;
std::unique_ptr<TestWsOrderManager> WsOrderManagerTest::order_manager_;

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
  request.order_qty = QtyType::from_double(1.5);
  request.price = PriceType::from_double(50000.00);
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
  request.order_qty = QtyType::from_double(2.0);
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
  request.order_qty = QtyType::from_double(1.0);
  request.price = PriceType::from_double(50000.00);

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
  request.order_qty = QtyType::from_double(10.0);
  request.price = PriceType::from_double(60000.00);
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
  request.order_qty = QtyType::from_double(1.0);
  request.price = PriceType::from_double(50000.00);
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
  if (json.empty()) {
    GTEST_SKIP() << "place_order_fail.json not available";
  }

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
  request.order_qty = QtyType::from_double(10.0);
  request.price = PriceType::from_double(60000.00);
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
  if (json.empty()) {
    GTEST_SKIP() << "cancel_order_response_fail.json not available";
  }

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
  request.order_qty = QtyType::from_double(5.0);
  request.price = PriceType::from_double(3000.00);
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
  if (json.empty()) {
    GTEST_SKIP() << "place_order_fail.json not available";
  }

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

  if (place_json.empty() || cancel_json.empty()) {
    GTEST_SKIP() << "Required test data files not available";
  }

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
  request1.order_qty = QtyType::from_double(1.0);
  request1.price = PriceType::from_double(50000.00);
  request1.time_in_force = TimeInForce::kGoodTillCancel;

  PendingOrderRequest request2;
  request2.client_order_id = 1764653550514761441ULL;
  request2.symbol = "ETHUSDT";
  request2.side = Side::kSell;
  request2.ord_type = OrderType::kMarket;
  request2.order_qty = QtyType::from_double(2.0);

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

// ============================================================================
// Cancel-and-Reorder Pair Tracking Tests
// ============================================================================

TEST_F(WsOrderManagerTest, RegisterCancelAndReorderPair_Success) {
  uint64_t new_order_id = 1111111111;
  uint64_t original_order_id = 2222222222;

  order_manager_->register_cancel_and_reorder_pair(new_order_id, original_order_id);

  auto result = order_manager_->get_original_order_id(new_order_id);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), original_order_id);
}

TEST_F(WsOrderManagerTest, GetOriginalOrderId_NotFound_ReturnsNullopt) {
  uint64_t non_existent_order_id = 9999999999;

  auto result = order_manager_->get_original_order_id(non_existent_order_id);
  EXPECT_FALSE(result.has_value());
}

TEST_F(WsOrderManagerTest, RemoveCancelAndReorderPair_Success) {
  uint64_t new_order_id = 3333333333;
  uint64_t original_order_id = 4444444444;

  order_manager_->register_cancel_and_reorder_pair(new_order_id, original_order_id);

  // Verify it's registered
  auto result1 = order_manager_->get_original_order_id(new_order_id);
  ASSERT_TRUE(result1.has_value());
  EXPECT_EQ(result1.value(), original_order_id);

  // Remove the pair
  order_manager_->remove_cancel_and_reorder_pair(new_order_id);

  // Verify it's removed
  auto result2 = order_manager_->get_original_order_id(new_order_id);
  EXPECT_FALSE(result2.has_value());
}

TEST_F(WsOrderManagerTest, RemoveCancelAndReorderPair_NonExistent_NoEffect) {
  uint64_t non_existent_order_id = 8888888888;

  // Should not crash
  order_manager_->remove_cancel_and_reorder_pair(non_existent_order_id);
}

TEST_F(WsOrderManagerTest, CancelAndReorderPair_MultiplePairs_IndependentTracking) {
  uint64_t new_order_id1 = 1000000001;
  uint64_t original_order_id1 = 2000000001;
  uint64_t new_order_id2 = 1000000002;
  uint64_t original_order_id2 = 2000000002;
  uint64_t new_order_id3 = 1000000003;
  uint64_t original_order_id3 = 2000000003;

  // Register 3 pairs
  order_manager_->register_cancel_and_reorder_pair(new_order_id1, original_order_id1);
  order_manager_->register_cancel_and_reorder_pair(new_order_id2, original_order_id2);
  order_manager_->register_cancel_and_reorder_pair(new_order_id3, original_order_id3);

  // Verify all pairs
  EXPECT_EQ(order_manager_->get_original_order_id(new_order_id1).value(), original_order_id1);
  EXPECT_EQ(order_manager_->get_original_order_id(new_order_id2).value(), original_order_id2);
  EXPECT_EQ(order_manager_->get_original_order_id(new_order_id3).value(), original_order_id3);

  // Remove middle pair
  order_manager_->remove_cancel_and_reorder_pair(new_order_id2);

  // Verify removal
  EXPECT_TRUE(order_manager_->get_original_order_id(new_order_id1).has_value());
  EXPECT_FALSE(order_manager_->get_original_order_id(new_order_id2).has_value());
  EXPECT_TRUE(order_manager_->get_original_order_id(new_order_id3).has_value());

  // Cleanup
  order_manager_->remove_cancel_and_reorder_pair(new_order_id1);
  order_manager_->remove_cancel_and_reorder_pair(new_order_id3);
}

// ============================================================================
// Cancel-and-Reorder Real JSON Integration Tests
// ============================================================================

TEST_F(WsOrderManagerTest, RealJson_CancelAndReorder_CancelSuccessNewFailure) {
  std::string json = test_utils::load_test_data("cancel_reorder_fail.json");
  if (json.empty()) {
    GTEST_SKIP() << "cancel_reorder_fail.json not available";
  }

  // Parse JSON
  schema::CancelAndReorderResponse response;
  auto ec = glz::read_json(response, json);
  ASSERT_FALSE(ec) << "Failed to parse JSON: " << glz::format_error(ec, json);

  // Verify JSON structure
  EXPECT_EQ(response.id, "orderreplace_1764690263119909563");
  EXPECT_EQ(response.status, 409);
  ASSERT_TRUE(response.error.has_value());
  EXPECT_EQ(response.error->code, -2021);
  EXPECT_EQ(response.error->message, "Order cancel-replace partially failed.");

  // Extract IDs from JSON
  // New order ID is in the request ID
  auto new_order_id_opt = TestWsOrderManager::extract_client_order_id(response.id);
  ASSERT_TRUE(new_order_id_opt.has_value());
  uint64_t new_order_id = new_order_id_opt.value();
  EXPECT_EQ(new_order_id, 1764690263119909563ULL);

  // Original order ID is in cancelResponse.origClientOrderId
  // For testing, we'll use a known original order ID
  uint64_t original_order_id = 1764690263066988543ULL;

  // Register cancel-and-reorder pair
  order_manager_->register_cancel_and_reorder_pair(new_order_id, original_order_id);

  // Register pending requests for both orders
  PendingOrderRequest new_order_request;
  new_order_request.client_order_id = new_order_id;
  new_order_request.symbol = "BTCUSDT";
  new_order_request.side = Side::kBuy;
  new_order_request.ord_type = OrderType::kLimit;
  new_order_request.order_qty = QtyType::from_double(0.00006);
  new_order_request.price = PriceType::from_double(90636.16);
  new_order_request.time_in_force = TimeInForce::kGoodTillCancel;
  order_manager_->register_pending_request(new_order_request);

  PendingOrderRequest cancel_request;
  cancel_request.client_order_id = original_order_id;
  cancel_request.symbol = "BTCUSDT";
  cancel_request.side = Side::kBuy;
  cancel_request.price = PriceType::from_double(0.0);
  cancel_request.order_qty = QtyType::from_double(0.0);
  cancel_request.ord_type = OrderType::kInvalid;
  cancel_request.time_in_force = TimeInForce::kInvalid;
  order_manager_->register_pending_request(cancel_request);

  // Verify error.data exists
  ASSERT_TRUE(response.error->data.has_value());
  const auto& error_data = response.error->data.value();
  EXPECT_EQ(error_data.cancel_result, "SUCCESS");
  EXPECT_EQ(error_data.new_order_result, "FAILURE");

  // Verify cancel response is CancelSuccess (variant index 2)
  EXPECT_EQ(error_data.cancel_response.index(), 2);
  const auto& cancel_success = std::get<schema::CancelSuccess>(error_data.cancel_response);
  EXPECT_EQ(cancel_success.symbol, "BTCUSDT");
  EXPECT_EQ(cancel_success.orig_client_order_id, "1764690263066988543");
  EXPECT_EQ(cancel_success.status, "CANCELED");

  // Verify new order response is ShortError (variant index 1)
  EXPECT_EQ(error_data.new_order_response.index(), 1);
  const auto& new_order_error = std::get<schema::ShortError>(error_data.new_order_response);
  EXPECT_EQ(new_order_error.code, -2010);
  EXPECT_EQ(new_order_error.msg, "Account has insufficient balance for requested action.");

  // Create synthetic report for NEW order failure
  std::string new_order_request_id = "orderreplace_" + std::to_string(new_order_id);
  auto new_order_synthetic = order_manager_->create_synthetic_execution_report(
      new_order_request_id, new_order_error.code, new_order_error.msg);

  ASSERT_TRUE(new_order_synthetic.has_value());
  EXPECT_EQ(new_order_synthetic->event.client_order_id, new_order_id);
  EXPECT_EQ(new_order_synthetic->event.execution_type, "REJECTED");
  EXPECT_EQ(new_order_synthetic->event.order_status, "REJECTED");
  EXPECT_EQ(new_order_synthetic->event.reject_reason, "Account has insufficient balance for requested action.");
  EXPECT_EQ(new_order_synthetic->event.symbol, "BTCUSDT");
  EXPECT_EQ(new_order_synthetic->event.side, "BUY");

  // Verify pair can retrieve original order ID
  auto retrieved_original = order_manager_->get_original_order_id(new_order_id);
  ASSERT_TRUE(retrieved_original.has_value());
  EXPECT_EQ(retrieved_original.value(), original_order_id);

  // Cleanup - remove pending request for CANCEL order (SUCCESS case)
  order_manager_->remove_pending_request(original_order_id);

  // Cleanup - remove pair
  order_manager_->remove_cancel_and_reorder_pair(new_order_id);

  // Verify cleanup
  EXPECT_FALSE(order_manager_->get_original_order_id(new_order_id).has_value());
}

TEST_F(WsOrderManagerTest, RealJson_CancelAndReorder_CancelFailureNewNotAttempted) {
  std::string json = test_utils::load_test_data("cancel_reorder_cancel_fail.json");
  if (json.empty()) {
    GTEST_SKIP() << "cancel_reorder_cancel_fail.json not available";
  }

  // Parse JSON
  schema::CancelAndReorderResponse response;
  auto ec = glz::read_json(response, json);
  ASSERT_FALSE(ec) << "Failed to parse JSON: " << glz::format_error(ec, json);

  // Verify JSON structure
  EXPECT_EQ(response.id, "orderreplace_1764722955000111");
  EXPECT_EQ(response.status, 400);
  ASSERT_TRUE(response.error.has_value());
  EXPECT_EQ(response.error->code, -2022);
  EXPECT_EQ(response.error->message, "Order cancel-replace failed.");

  // Extract new order ID from request ID
  auto new_order_id_opt = TestWsOrderManager::extract_client_order_id(response.id);
  ASSERT_TRUE(new_order_id_opt.has_value());
  uint64_t new_order_id = new_order_id_opt.value();
  EXPECT_EQ(new_order_id, 1764722955000111ULL);

  // Original order ID - for testing, we need to know it beforehand
  // In real scenario, this comes from WsOeApp::post_cancel_and_reorder
  uint64_t original_order_id = 1764722955000000ULL;

  // Register cancel-and-reorder pair
  order_manager_->register_cancel_and_reorder_pair(new_order_id, original_order_id);

  // Register pending requests for both orders
  PendingOrderRequest new_order_request;
  new_order_request.client_order_id = new_order_id;
  new_order_request.symbol = "BTCUSDT";
  new_order_request.side = Side::kBuy;
  new_order_request.ord_type = OrderType::kLimit;
  new_order_request.order_qty = QtyType::from_double(0.00010);
  new_order_request.price = PriceType::from_double(90000.00);
  new_order_request.time_in_force = TimeInForce::kGoodTillCancel;
  order_manager_->register_pending_request(new_order_request);

  PendingOrderRequest cancel_request;
  cancel_request.client_order_id = original_order_id;
  cancel_request.symbol = "BTCUSDT";
  cancel_request.side = Side::kBuy;
  cancel_request.price = PriceType::from_double(0.0);
  cancel_request.order_qty = QtyType::from_double(0.0);
  cancel_request.ord_type = OrderType::kInvalid;
  cancel_request.time_in_force = TimeInForce::kInvalid;
  order_manager_->register_pending_request(cancel_request);

  // Verify error.data exists
  ASSERT_TRUE(response.error->data.has_value());
  const auto& error_data = response.error->data.value();
  EXPECT_EQ(error_data.cancel_result, "FAILURE");
  EXPECT_EQ(error_data.new_order_result, "NOT_ATTEMPTED");

  // Verify cancel response is ShortError (variant index 1)
  EXPECT_EQ(error_data.cancel_response.index(), 1);
  const auto& cancel_error = std::get<schema::ShortError>(error_data.cancel_response);
  EXPECT_EQ(cancel_error.code, -2011);
  EXPECT_EQ(cancel_error.msg, "Unknown order sent.");

  // Verify new order response is monostate (variant index 0 - null)
  EXPECT_EQ(error_data.new_order_response.index(), 0);

  // Use pair tracking to get original order ID
  auto retrieved_original = order_manager_->get_original_order_id(new_order_id);
  ASSERT_TRUE(retrieved_original.has_value());
  EXPECT_EQ(retrieved_original.value(), original_order_id);

  // Create synthetic report for CANCEL failure using original order ID
  std::string cancel_request_id = "ordercancel_" + std::to_string(original_order_id);
  auto cancel_synthetic = order_manager_->create_synthetic_execution_report(
      cancel_request_id, cancel_error.code, cancel_error.msg);

  ASSERT_TRUE(cancel_synthetic.has_value());
  EXPECT_EQ(cancel_synthetic->event.client_order_id, original_order_id);
  EXPECT_EQ(cancel_synthetic->event.execution_type, "REJECTED");
  EXPECT_EQ(cancel_synthetic->event.order_status, "REJECTED");
  EXPECT_EQ(cancel_synthetic->event.reject_reason, "Unknown order sent.");
  EXPECT_EQ(cancel_synthetic->event.symbol, "BTCUSDT");
  EXPECT_EQ(cancel_synthetic->event.side, "BUY");

  // Cleanup - NEW order was NOT_ATTEMPTED, so just remove pending request
  order_manager_->remove_pending_request(new_order_id);

  // Cleanup - remove pair
  order_manager_->remove_cancel_and_reorder_pair(new_order_id);

  // Verify cleanup
  EXPECT_FALSE(order_manager_->get_original_order_id(new_order_id).has_value());
}

TEST_F(WsOrderManagerTest, CancelAndReorder_MemoryLeakPrevention_AllScenarios) {
  // This test verifies that both pending requests and pairs are cleaned up
  // in all cancel-and-reorder scenarios to prevent memory leaks

  uint64_t new_order_id = 5000000001;
  uint64_t original_order_id = 5000000002;

  // Register pair
  order_manager_->register_cancel_and_reorder_pair(new_order_id, original_order_id);

  // Register both pending requests
  PendingOrderRequest new_order_request;
  new_order_request.client_order_id = new_order_id;
  new_order_request.symbol = "BTCUSDT";
  new_order_request.side = Side::kBuy;
  new_order_request.ord_type = OrderType::kLimit;
  new_order_request.order_qty = QtyType::from_double(1.0);
  new_order_request.price = PriceType::from_double(50000.00);
  new_order_request.time_in_force = TimeInForce::kGoodTillCancel;
  order_manager_->register_pending_request(new_order_request);

  PendingOrderRequest cancel_request;
  cancel_request.client_order_id = original_order_id;
  cancel_request.symbol = "BTCUSDT";
  cancel_request.side = Side::kBuy;
  cancel_request.price = PriceType::from_double(0.0);
  cancel_request.order_qty = QtyType::from_double(0.0);
  cancel_request.ord_type = OrderType::kInvalid;
  cancel_request.time_in_force = TimeInForce::kInvalid;
  order_manager_->register_pending_request(cancel_request);

  // Verify pair exists
  ASSERT_TRUE(order_manager_->get_original_order_id(new_order_id).has_value());

  // Simulate cleanup (as done in handle_cancel_and_reorder_response)
  // Remove both pending requests
  order_manager_->remove_pending_request(new_order_id);
  order_manager_->remove_pending_request(original_order_id);

  // Remove pair
  order_manager_->remove_cancel_and_reorder_pair(new_order_id);

  // Verify everything is cleaned up
  EXPECT_FALSE(order_manager_->get_original_order_id(new_order_id).has_value());

  // Verify synthetic reports now return minimal data (pending requests removed)
  auto synthetic1 = order_manager_->create_synthetic_execution_report(
      "orderreplace_" + std::to_string(new_order_id), -2010, "Error");
  ASSERT_TRUE(synthetic1.has_value());
  EXPECT_EQ(synthetic1->event.symbol, "");  // Minimal data

  auto synthetic2 = order_manager_->create_synthetic_execution_report(
      "ordercancel_" + std::to_string(original_order_id), -2011, "Error");
  ASSERT_TRUE(synthetic2.has_value());
  EXPECT_EQ(synthetic2->event.symbol, "");  // Minimal data
}

// ============================================================================
// TBB Concurrent Hash Map - Thread Safety Tests
// ============================================================================

class WsOrderManagerConcurrencyTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    logger_ = std::make_unique<Logger>();
    logger_->setLevel(LogLevel::kWarn);
    logger_->clearSink();
    producer_ = std::make_unique<Logger::Producer>(logger_->make_producer());
    order_manager_ = std::make_unique<TestWsOrderManager>(*producer_);
  }

  static void TearDownTestSuite() {
    order_manager_.reset();
    producer_.reset();
    logger_->shutdown();
    logger_.reset();
  }

  static std::unique_ptr<Logger> logger_;
  static std::unique_ptr<Logger::Producer> producer_;
  static std::unique_ptr<TestWsOrderManager> order_manager_;
};

std::unique_ptr<Logger> WsOrderManagerConcurrencyTest::logger_;
std::unique_ptr<Logger::Producer> WsOrderManagerConcurrencyTest::producer_;
std::unique_ptr<TestWsOrderManager> WsOrderManagerConcurrencyTest::order_manager_;

TEST_F(WsOrderManagerConcurrencyTest, ConcurrentRegisterAndRemove_NoDeadlock) {
  // Simulates TradeEngine (register) and OEStream (remove) concurrent access
  constexpr int kNumOperations = 10000;
  std::atomic<int> register_count{0};
  std::atomic<int> remove_count{0};

  // Producer thread (TradeEngine) - registers pending requests
  std::thread producer([&]() {
    for (int i = 0; i < kNumOperations; ++i) {
      PendingOrderRequest request;
      request.client_order_id = static_cast<uint64_t>(i);
      request.symbol = "BTCUSDT";
      request.side = Side::kBuy;
      request.ord_type = OrderType::kLimit;
      request.order_qty = QtyType::from_double(1.0);
      request.price = PriceType::from_double(50000.0);
      request.time_in_force = TimeInForce::kGoodTillCancel;

      order_manager_->register_pending_request(request);
      register_count.fetch_add(1, std::memory_order_relaxed);
    }
  });

  // Consumer thread (OEStream) - removes pending requests
  std::thread consumer([&]() {
    int removed = 0;
    while (removed < kNumOperations) {
      for (int i = 0; i < kNumOperations; ++i) {
        order_manager_->remove_pending_request(static_cast<uint64_t>(i));
      }
      removed = remove_count.load(std::memory_order_relaxed);
      // Count actual removals by checking current register count
      int current_registered = register_count.load(std::memory_order_relaxed);
      if (current_registered >= kNumOperations) {
        // All registered, do one final pass
        for (int i = 0; i < kNumOperations; ++i) {
          order_manager_->remove_pending_request(static_cast<uint64_t>(i));
        }
        break;
      }
      std::this_thread::yield();
    }
  });

  producer.join();
  consumer.join();

  EXPECT_EQ(register_count.load(), kNumOperations);
}

TEST_F(WsOrderManagerConcurrencyTest, ConcurrentRegisterAndSyntheticReport_NoDeadlock) {
  // Simulates TradeEngine (register) and OEApi (create_synthetic) concurrent access
  constexpr int kNumOperations = 5000;
  std::atomic<int> completed{0};
  std::atomic<bool> producer_done{false};

  // Producer thread (TradeEngine)
  std::thread producer([&]() {
    for (int i = 0; i < kNumOperations; ++i) {
      PendingOrderRequest request;
      request.client_order_id = static_cast<uint64_t>(i + 100000);
      request.symbol = "ETHUSDT";
      request.side = Side::kSell;
      request.ord_type = OrderType::kMarket;
      request.order_qty = QtyType::from_double(2.0);
      request.time_in_force = TimeInForce::kImmediateOrCancel;

      order_manager_->register_pending_request(request);
    }
    producer_done.store(true, std::memory_order_release);
  });

  // Consumer thread (OEApi/OEStream)
  std::thread consumer([&]() {
    int processed = 0;
    while (!producer_done.load(std::memory_order_acquire) || processed < kNumOperations) {
      for (int i = 0; i < kNumOperations && processed < kNumOperations; ++i) {
        std::string request_id = "orderplace_" + std::to_string(i + 100000);
        auto result = order_manager_->create_synthetic_execution_report(
            request_id, -2010, "Test error");
        if (result.has_value()) {
          processed++;
        }
      }
      if (processed < kNumOperations) {
        std::this_thread::yield();
      }
    }
    completed.store(processed, std::memory_order_relaxed);
  });

  producer.join();
  consumer.join();

  // All operations should complete without deadlock
  EXPECT_EQ(completed.load(), kNumOperations);
}

TEST_F(WsOrderManagerConcurrencyTest, ConcurrentCancelReorderPairOperations_NoDeadlock) {
  // Test concurrent access to cancel_reorder_pairs map
  constexpr int kNumOperations = 5000;
  std::atomic<int> register_done{0};
  std::atomic<int> lookup_done{0};
  std::atomic<int> remove_done{0};

  // Thread 1: Register pairs
  std::thread registerer([&]() {
    for (int i = 0; i < kNumOperations; ++i) {
      order_manager_->register_cancel_and_reorder_pair(
          static_cast<uint64_t>(i + 200000),
          static_cast<uint64_t>(i + 300000));
      register_done.fetch_add(1, std::memory_order_relaxed);
    }
  });

  // Thread 2: Lookup pairs
  std::thread looker([&]() {
    int found = 0;
    while (found < kNumOperations) {
      for (int i = 0; i < kNumOperations; ++i) {
        auto result = order_manager_->get_original_order_id(
            static_cast<uint64_t>(i + 200000));
        if (result.has_value()) {
          found++;
          lookup_done.fetch_add(1, std::memory_order_relaxed);
        }
      }
      if (register_done.load(std::memory_order_relaxed) >= kNumOperations) {
        break;
      }
      std::this_thread::yield();
    }
  });

  // Thread 3: Remove pairs (starts after some registrations)
  std::thread remover([&]() {
    // Wait for some registrations
    while (register_done.load(std::memory_order_relaxed) < kNumOperations / 2) {
      std::this_thread::yield();
    }

    for (int i = 0; i < kNumOperations; ++i) {
      order_manager_->remove_cancel_and_reorder_pair(
          static_cast<uint64_t>(i + 200000));
      remove_done.fetch_add(1, std::memory_order_relaxed);
    }
  });

  registerer.join();
  looker.join();
  remover.join();

  EXPECT_EQ(register_done.load(), kNumOperations);
  EXPECT_EQ(remove_done.load(), kNumOperations);
}

TEST_F(WsOrderManagerConcurrencyTest, MultipleProducersMultipleConsumers_NoDeadlock) {
  // Stress test with multiple producers and consumers
  constexpr int kNumProducers = 4;
  constexpr int kNumConsumers = 4;
  constexpr int kOpsPerThread = 2000;

  std::atomic<int> total_registered{0};
  std::atomic<int> total_removed{0};
  std::vector<std::thread> threads;

  // Producers
  for (int p = 0; p < kNumProducers; ++p) {
    threads.emplace_back([&, p]() {
      for (int i = 0; i < kOpsPerThread; ++i) {
        uint64_t id = static_cast<uint64_t>(p * kOpsPerThread + i + 400000);

        PendingOrderRequest request;
        request.client_order_id = id;
        request.symbol = "BTCUSDT";
        request.side = (i % 2 == 0) ? Side::kBuy : Side::kSell;
        request.ord_type = OrderType::kLimit;
        request.order_qty = QtyType::from_double(1.0);
        request.price = PriceType::from_double(50000.0);
        request.time_in_force = TimeInForce::kGoodTillCancel;

        order_manager_->register_pending_request(request);
        total_registered.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }

  // Consumers
  for (int c = 0; c < kNumConsumers; ++c) {
    threads.emplace_back([&, c]() {
      // Each consumer handles a portion of the IDs
      for (int i = 0; i < kOpsPerThread * kNumProducers / kNumConsumers; ++i) {
        uint64_t id = static_cast<uint64_t>(c * kOpsPerThread + i + 400000);
        order_manager_->remove_pending_request(id);
        total_removed.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  EXPECT_EQ(total_registered.load(), kNumProducers * kOpsPerThread);
}

TEST_F(WsOrderManagerConcurrencyTest, RapidRegisterRemoveSameKey_NoDeadlock) {
  // Test rapid register/remove of same key - potential deadlock scenario
  constexpr int kIterations = 10000;
  constexpr uint64_t kTestId = 999999;

  std::atomic<bool> stop{false};
  std::atomic<int> register_ops{0};
  std::atomic<int> remove_ops{0};

  std::thread producer([&]() {
    for (int i = 0; i < kIterations && !stop.load(std::memory_order_relaxed); ++i) {
      PendingOrderRequest request;
      request.client_order_id = kTestId;
      request.symbol = "BTCUSDT";
      request.side = Side::kBuy;
      request.ord_type = OrderType::kLimit;
      request.order_qty = QtyType::from_double(1.0);
      request.price = PriceType::from_double(50000.0);
      request.time_in_force = TimeInForce::kGoodTillCancel;

      order_manager_->register_pending_request(request);
      register_ops.fetch_add(1, std::memory_order_relaxed);
    }
  });

  std::thread consumer([&]() {
    for (int i = 0; i < kIterations && !stop.load(std::memory_order_relaxed); ++i) {
      order_manager_->remove_pending_request(kTestId);
      remove_ops.fetch_add(1, std::memory_order_relaxed);
    }
  });

  // Timeout detection
  auto start = std::chrono::steady_clock::now();
  while (register_ops.load() < kIterations || remove_ops.load() < kIterations) {
    auto elapsed = std::chrono::steady_clock::now() - start;
    if (elapsed > std::chrono::seconds(10)) {
      stop.store(true, std::memory_order_relaxed);
      FAIL() << "Deadlock detected - operations did not complete in 10 seconds";
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  producer.join();
  consumer.join();

  EXPECT_EQ(register_ops.load(), kIterations);
  EXPECT_EQ(remove_ops.load(), kIterations);
}

TEST_F(WsOrderManagerConcurrencyTest, ConcurrentSyntheticReportCreation_DataIntegrity) {
  // Verify data integrity under concurrent access
  constexpr int kNumOrders = 1000;

  // Pre-register all pending requests
  for (int i = 0; i < kNumOrders; ++i) {
    PendingOrderRequest request;
    request.client_order_id = static_cast<uint64_t>(i + 600000);
    request.symbol = "BTCUSDT_" + std::to_string(i);  // Unique symbol per order
    request.side = Side::kBuy;
    request.ord_type = OrderType::kLimit;
    request.order_qty = QtyType::from_double(static_cast<double>(i));
    request.price = PriceType::from_double(static_cast<double>(i * 100));
    request.time_in_force = TimeInForce::kGoodTillCancel;

    order_manager_->register_pending_request(request);
  }

  std::atomic<int> integrity_errors{0};
  std::vector<std::thread> threads;

  // Multiple threads creating synthetic reports
  for (int t = 0; t < 4; ++t) {
    threads.emplace_back([&, t]() {
      for (int i = t * (kNumOrders / 4); i < (t + 1) * (kNumOrders / 4); ++i) {
        std::string request_id = "orderplace_" + std::to_string(i + 600000);
        auto result = order_manager_->create_synthetic_execution_report(
            request_id, -2010, "Test error");

        if (result.has_value()) {
          // Verify data integrity
          std::string expected_symbol = "BTCUSDT_" + std::to_string(i);
          if (result->event.symbol != expected_symbol) {
            integrity_errors.fetch_add(1, std::memory_order_relaxed);
          }
          if (result->event.order_quantity != static_cast<double>(i)) {
            integrity_errors.fetch_add(1, std::memory_order_relaxed);
          }
          if (result->event.order_price != static_cast<double>(i * 100)) {
            integrity_errors.fetch_add(1, std::memory_order_relaxed);
          }
        }
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  EXPECT_EQ(integrity_errors.load(), 0) << "Data integrity errors detected";
}
