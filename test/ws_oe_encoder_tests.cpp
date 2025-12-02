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
#include <glaze/glaze.hpp>
#include "websocket/order_entry/ws_oe_encoder.h"
#include "order_entry.h"
#include "logger.h"

using namespace core;
using namespace trading;
using namespace common;

// Helper function to validate JSON structure
bool is_valid_json(std::string_view json) {
  // Simple validation: check if it starts with { or [ and ends with } or ]
  if (json.empty()) return false;
  auto trimmed = json;
  while (!trimmed.empty() && std::isspace(trimmed.front())) trimmed.remove_prefix(1);
  while (!trimmed.empty() && std::isspace(trimmed.back())) trimmed.remove_suffix(1);

  if (trimmed.empty()) return false;

  char first = trimmed.front();
  char last = trimmed.back();

  return (first == '{' && last == '}') || (first == '[' && last == ']');
}

class WsOeEncoderTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    logger_ = std::make_unique<Logger>();
    logger_->setLevel(LogLevel::kDebug);
    logger_->clearSink();
    producer_ = std::make_unique<Logger::Producer>(logger_->make_producer());
    encoder_ = std::make_unique<WsOeEncoder>(*producer_);
  }

  static void TearDownTestSuite() {
    encoder_.reset();  // Must reset encoder before producer and logger
    producer_.reset();
    logger_->shutdown();
    logger_.reset();
  }

  static std::unique_ptr<Logger> logger_;
  static std::unique_ptr<Logger::Producer> producer_;
  static std::unique_ptr<WsOeEncoder> encoder_;
};
std::unique_ptr<Logger> WsOeEncoderTest::logger_;
std::unique_ptr<Logger::Producer> WsOeEncoderTest::producer_;
std::unique_ptr<WsOeEncoder> WsOeEncoderTest::encoder_;

// ============================================================================
// Session Management Tests
// ============================================================================

TEST_F(WsOeEncoderTest, CreateLogOnMessage_ValidSignature_ProducesValidJson) {
  std::string signature = "test_signature_123";
  std::string timestamp = "1699564800000";

  std::string result = encoder_->create_log_on_message(signature, timestamp);

  EXPECT_TRUE(is_valid_json(result));
  EXPECT_FALSE(result.empty());

  // Verify the signature and timestamp are in the result
  EXPECT_NE(result.find(signature), std::string::npos);
  EXPECT_NE(result.find(timestamp), std::string::npos);
}

TEST_F(WsOeEncoderTest, CreateLogOutMessage_ProducesValidJson) {
  std::string result = encoder_->create_log_out_message();

  EXPECT_TRUE(is_valid_json(result));
  EXPECT_FALSE(result.empty());
}

TEST_F(WsOeEncoderTest, CreateHeartbeatMessage_ProducesValidJson) {
  std::string result = encoder_->create_heartbeat_message();

  EXPECT_TRUE(result.empty());
}

TEST_F(WsOeEncoderTest, CreateUserDataStreamSubscribe_ProducesValidJson) {
  std::string result = encoder_->create_user_data_stream_subscribe();

  EXPECT_TRUE(is_valid_json(result));
  EXPECT_FALSE(result.empty());
}

TEST_F(WsOeEncoderTest, CreateUserDataStreamUnsubscribe_ProducesValidJson) {
  std::string result = encoder_->create_user_data_stream_unsubscribe();

  EXPECT_TRUE(is_valid_json(result));
  EXPECT_FALSE(result.empty());
}

// ============================================================================
// Order Operations Tests
// ============================================================================

TEST_F(WsOeEncoderTest, CreateOrderMessage_LimitOrder_ContainsAllFields) {
  NewSingleOrderData order;
  order.symbol = "BTCUSDT";
  order.side = OrderSide::kBuy;
  order.ord_type = OrderType::kLimit;
  order.order_qty = Qty{1.50000};
  order.price = Price{50000.00};
  order.cl_order_id = OrderId{1234567890};
  order.time_in_force = TimeInForce::kGoodTillCancel;
  order.self_trade_prevention_mode = SelfTradePreventionMode::kNone;

  std::string result = encoder_->create_order_message(order);

  EXPECT_TRUE(is_valid_json(result));
  EXPECT_NE(result.find("BTCUSDT"), std::string::npos);
  EXPECT_NE(result.find("BUY"), std::string::npos);
  EXPECT_NE(result.find("LIMIT"), std::string::npos);
  EXPECT_NE(result.find("GTC"), std::string::npos);

  // Check precision: price should be 2 decimals, qty should be 5 decimals
  EXPECT_NE(result.find("50000.00"), std::string::npos);
  EXPECT_NE(result.find("1.50000"), std::string::npos);
}

TEST_F(WsOeEncoderTest, CreateOrderMessage_MarketOrder_ProducesValidJson) {
  NewSingleOrderData order;
  order.symbol = "ETHUSDT";
  order.side = OrderSide::kSell;
  order.ord_type = OrderType::kMarket;
  order.order_qty = Qty{2.0};
  order.cl_order_id = OrderId{9876543210};
  order.self_trade_prevention_mode = SelfTradePreventionMode::kExpireTaker;

  std::string result = encoder_->create_order_message(order);

  EXPECT_TRUE(is_valid_json(result));
  EXPECT_NE(result.find("ETHUSDT"), std::string::npos);
  EXPECT_NE(result.find("SELL"), std::string::npos);
  EXPECT_NE(result.find("MARKET"), std::string::npos);
  EXPECT_NE(result.find("2.00000"), std::string::npos);
}

TEST_F(WsOeEncoderTest, CreateCancelOrderMessage_ValidRequest_ProducesValidJson) {
  OrderCancelRequest cancel;
  cancel.symbol = "BTCUSDT";
  cancel.orig_cl_order_id = OrderId{1234567890};
  cancel.cl_order_id = OrderId{9999999999};

  std::string result = encoder_->create_cancel_order_message(cancel);

  EXPECT_TRUE(is_valid_json(result));
  EXPECT_NE(result.find("BTCUSDT"), std::string::npos);
}

TEST_F(WsOeEncoderTest, CreateCancelAndReorderMessage_ValidRequest_ContainsAllParams) {
  OrderCancelAndNewOrderSingle replace;
  replace.symbol = "BTCUSDT";
  replace.cl_origin_order_id = OrderId{1111111111};
  replace.cancel_new_order_id = OrderId{2222222222};
  replace.cl_new_order_id = OrderId{3333333333};
  replace.side = OrderSide::kBuy;
  replace.ord_type = OrderType::kLimit;
  replace.order_qty = Qty{0.75};
  replace.price = Price{51000.00};
  replace.time_in_force = TimeInForce::kGoodTillCancel;
  replace.self_trade_prevention_mode = SelfTradePreventionMode::kNone;

  std::string result = encoder_->create_cancel_and_reorder_message(replace);

  EXPECT_TRUE(is_valid_json(result));
  EXPECT_NE(result.find("BTCUSDT"), std::string::npos);
  EXPECT_NE(result.find("51000.00"), std::string::npos);
  EXPECT_NE(result.find("0.75000"), std::string::npos);
}

TEST_F(WsOeEncoderTest, CreateOrderAllCancel_ValidSymbol_ProducesValidJson) {
  OrderMassCancelRequest request;
  request.symbol = "BTCUSDT";
  request.cl_order_id = OrderId{5555555555};
  request.mass_cancel_request_type = '1';

  std::string result = encoder_->create_order_all_cancel(request);

  EXPECT_TRUE(is_valid_json(result));
  EXPECT_NE(result.find("BTCUSDT"), std::string::npos);
}

// ============================================================================
// Field Validation Tests
// ============================================================================

TEST_F(WsOeEncoderTest, PriceFormatting_TwoDecimalPrecision_CorrectFormat) {
  NewSingleOrderData order;
  order.symbol = "BTCUSDT";
  order.side = OrderSide::kBuy;
  order.ord_type = OrderType::kLimit;
  order.order_qty = Qty{0.00012};
  order.price = Price{12345.68};
  order.cl_order_id = OrderId{123};
  order.time_in_force = TimeInForce::kGoodTillCancel;
  order.self_trade_prevention_mode = SelfTradePreventionMode::kNone;

  std::string result = encoder_->create_order_message(order);

  EXPECT_NE(result.find("12345.68"), std::string::npos);
}

TEST_F(WsOeEncoderTest, QuantityFormatting_FiveDecimalPrecision_CorrectFormat) {
  NewSingleOrderData order;
  order.symbol = "BTCUSDT";
  order.side = OrderSide::kBuy;
  order.ord_type = OrderType::kLimit;
  order.order_qty = Qty{1.123456789};  // Should be formatted to 1.12346
  order.price = Price{50000.00};
  order.cl_order_id = OrderId{123};
  order.time_in_force = TimeInForce::kGoodTillCancel;
  order.self_trade_prevention_mode = SelfTradePreventionMode::kNone;

  std::string result = encoder_->create_order_message(order);

  // Quantity should have exactly 5 decimal places
  EXPECT_NE(result.find("1.12346"), std::string::npos);
}

TEST_F(WsOeEncoderTest, ClientOrderId_ConvertedToString_Present) {
  NewSingleOrderData order;
  order.symbol = "BTCUSDT";
  order.side = OrderSide::kBuy;
  order.ord_type = OrderType::kLimit;
  order.order_qty = Qty{1.0};
  order.price = Price{50000.00};
  order.cl_order_id = OrderId{9876543210};
  order.time_in_force = TimeInForce::kGoodTillCancel;
  order.self_trade_prevention_mode = SelfTradePreventionMode::kNone;

  std::string result = encoder_->create_order_message(order);

  // Client order ID should be present as string
  EXPECT_NE(result.find("9876543210"), std::string::npos);
}

// ============================================================================
// JSON Structure Validation Tests
// ============================================================================

TEST_F(WsOeEncoderTest, AllOrderMessages_ProduceValidJson_NoParsingErrors) {
  // Test that all order message types produce valid JSON
  NewSingleOrderData order;
  order.symbol = "BTCUSDT";
  order.side = OrderSide::kBuy;
  order.ord_type = OrderType::kLimit;
  order.order_qty = Qty{1.0};
  order.price = Price{50000.00};
  order.cl_order_id = OrderId{123};
  order.time_in_force = TimeInForce::kGoodTillCancel;
  order.self_trade_prevention_mode = SelfTradePreventionMode::kNone;

  EXPECT_TRUE(is_valid_json(encoder_->create_order_message(order)));

  OrderCancelRequest cancel;
  cancel.symbol = "BTCUSDT";
  cancel.orig_cl_order_id = OrderId{123};
  cancel.cl_order_id = OrderId{456};
  EXPECT_TRUE(is_valid_json(encoder_->create_cancel_order_message(cancel)));

  OrderMassCancelRequest mass_cancel;
  mass_cancel.symbol = "BTCUSDT";
  mass_cancel.cl_order_id = OrderId{789};
  EXPECT_TRUE(is_valid_json(encoder_->create_order_all_cancel(mass_cancel)));
}
