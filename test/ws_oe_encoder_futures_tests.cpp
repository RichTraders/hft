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
#include "websocket/order_entry/exchanges/binance/futures/binance_futures_oe_encoder.hpp"
#include "order_entry.h"
#include "logger.h"
#include "common/precision_config.hpp"

using namespace core;
using namespace trading;
using namespace common;
using PriceType = common::PriceType;
using QtyType = common::QtyType;

bool is_valid_json(std::string_view json) {
  if (json.empty()) return false;
  auto trimmed = json;
  while (!trimmed.empty() && std::isspace(trimmed.front())) trimmed.remove_prefix(1);
  while (!trimmed.empty() && std::isspace(trimmed.back())) trimmed.remove_suffix(1);

  if (trimmed.empty()) return false;

  char first = trimmed.front();
  char last = trimmed.back();

  return (first == '{' && last == '}') || (first == '[' && last == ']');
}

class WsOeFuturesEncoderTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    PRECISION_CONFIG.set_price_precision(2);
    PRECISION_CONFIG.set_qty_precision(5);

    logger_ = std::make_unique<Logger>();
    logger_->setLevel(LogLevel::kDebug);
    logger_->clearSink();
    producer_ = std::make_unique<Logger::Producer>(logger_->make_producer());
    encoder_ = std::make_unique<BinanceFuturesOeEncoder>(*producer_);
  }

  static void TearDownTestSuite() {
    encoder_.reset();
    producer_.reset();
    logger_->shutdown();
    logger_.reset();
  }

  static std::unique_ptr<Logger> logger_;
  static std::unique_ptr<Logger::Producer> producer_;
  static std::unique_ptr<BinanceFuturesOeEncoder> encoder_;
};
std::unique_ptr<Logger> WsOeFuturesEncoderTest::logger_;
std::unique_ptr<Logger::Producer> WsOeFuturesEncoderTest::producer_;
std::unique_ptr<BinanceFuturesOeEncoder> WsOeFuturesEncoderTest::encoder_;

// ============================================================================
// Session Management Tests
// ============================================================================

TEST_F(WsOeFuturesEncoderTest, CreateLogOnMessage_ValidSignature_ProducesValidJson) {
  std::string signature = "test_signature_123";
  std::string timestamp = "1699564800000";

  std::string result = encoder_->create_log_on_message(signature, timestamp);

  EXPECT_TRUE(is_valid_json(result));
  EXPECT_FALSE(result.empty());
  EXPECT_NE(result.find(signature), std::string::npos);
  EXPECT_NE(result.find(timestamp), std::string::npos);
}

TEST_F(WsOeFuturesEncoderTest, CreateLogOutMessage_ProducesValidJson) {
  std::string result = encoder_->create_log_out_message();

  EXPECT_TRUE(is_valid_json(result));
  EXPECT_FALSE(result.empty());
}

TEST_F(WsOeFuturesEncoderTest, CreateHeartbeatMessage_ProducesEmptyString) {
  std::string result = encoder_->create_heartbeat_message();
  EXPECT_TRUE(result.empty());
}

TEST_F(WsOeFuturesEncoderTest, CreateUserDataStreamSubscribe_ProducesValidJson) {
  std::string result = encoder_->create_user_data_stream_subscribe();

  EXPECT_TRUE(is_valid_json(result));
  EXPECT_FALSE(result.empty());
  EXPECT_NE(result.find("userDataStream.start"), std::string::npos);
}

TEST_F(WsOeFuturesEncoderTest, CreateUserDataStreamUnsubscribe_ProducesValidJson) {
  std::string result = encoder_->create_user_data_stream_unsubscribe();

  EXPECT_TRUE(is_valid_json(result));
  EXPECT_FALSE(result.empty());
  EXPECT_NE(result.find("userDataStream.stop"), std::string::npos);
}

TEST_F(WsOeFuturesEncoderTest, CreateUserDataStreamPing_ProducesValidJson) {
  std::string result = encoder_->create_user_data_stream_ping();

  EXPECT_TRUE(is_valid_json(result));
  EXPECT_FALSE(result.empty());
  EXPECT_NE(result.find("userDataStream.ping"), std::string::npos);
}

// ============================================================================
// Order Operations Tests
// ============================================================================

TEST_F(WsOeFuturesEncoderTest, CreateOrderMessage_LimitOrder_ContainsAllFields) {
  NewSingleOrderData order;
  order.symbol = "BTCUSDT";
  order.side = OrderSide::kBuy;
  order.ord_type = OrderType::kLimit;
  order.order_qty = QtyType::from_double(1.50000);
  order.price = PriceType::from_double(50000.00);
  order.cl_order_id = OrderId{1234567890};
  order.time_in_force = TimeInForce::kGoodTillCancel;
  order.self_trade_prevention_mode = SelfTradePreventionMode::kNone;
  order.position_side = PositionSide::kLong;

  std::string result = encoder_->create_order_message(order);

  EXPECT_TRUE(is_valid_json(result));
  EXPECT_NE(result.find("BTCUSDT"), std::string::npos);
  EXPECT_NE(result.find("BUY"), std::string::npos);
  EXPECT_NE(result.find("LIMIT"), std::string::npos);
  EXPECT_NE(result.find("GTC"), std::string::npos);
  EXPECT_NE(result.find("LONG"), std::string::npos);
}

TEST_F(WsOeFuturesEncoderTest, CreateOrderMessage_MarketOrder_ProducesValidJson) {
  NewSingleOrderData order;
  order.symbol = "ETHUSDT";
  order.side = OrderSide::kSell;
  order.ord_type = OrderType::kMarket;
  order.order_qty = QtyType::from_double(2.0);
  order.cl_order_id = OrderId{9876543210};
  order.self_trade_prevention_mode = SelfTradePreventionMode::kExpireTaker;
  order.position_side = PositionSide::kShort;

  std::string result = encoder_->create_order_message(order);

  EXPECT_TRUE(is_valid_json(result));
  EXPECT_NE(result.find("ETHUSDT"), std::string::npos);
  EXPECT_NE(result.find("SELL"), std::string::npos);
  EXPECT_NE(result.find("MARKET"), std::string::npos);
  EXPECT_NE(result.find("SHORT"), std::string::npos);
}

TEST_F(WsOeFuturesEncoderTest, CreateOrderMessage_PositionSideLong_IncludedInJson) {
  NewSingleOrderData order;
  order.symbol = "BTCUSDT";
  order.side = OrderSide::kBuy;
  order.ord_type = OrderType::kLimit;
  order.order_qty = QtyType::from_double(0.001);
  order.price = PriceType::from_double(50000.00);
  order.cl_order_id = OrderId{123};
  order.time_in_force = TimeInForce::kGoodTillCancel;
  order.self_trade_prevention_mode = SelfTradePreventionMode::kNone;
  order.position_side = PositionSide::kLong;

  std::string result = encoder_->create_order_message(order);

  EXPECT_NE(result.find("positionSide"), std::string::npos);
  EXPECT_NE(result.find("LONG"), std::string::npos);
}

TEST_F(WsOeFuturesEncoderTest, CreateOrderMessage_PositionSideShort_IncludedInJson) {
  NewSingleOrderData order;
  order.symbol = "BTCUSDT";
  order.side = OrderSide::kSell;
  order.ord_type = OrderType::kLimit;
  order.order_qty = QtyType::from_double(0.001);
  order.price = PriceType::from_double(50000.00);
  order.cl_order_id = OrderId{123};
  order.time_in_force = TimeInForce::kGoodTillCancel;
  order.self_trade_prevention_mode = SelfTradePreventionMode::kNone;
  order.position_side = PositionSide::kShort;

  std::string result = encoder_->create_order_message(order);

  EXPECT_NE(result.find("positionSide"), std::string::npos);
  EXPECT_NE(result.find("SHORT"), std::string::npos);
}

TEST_F(WsOeFuturesEncoderTest, CreateCancelOrderMessage_ValidRequest_ProducesValidJson) {
  OrderCancelRequest cancel;
  cancel.symbol = "BTCUSDT";
  cancel.orig_cl_order_id = OrderId{1234567890};
  cancel.cl_order_id = OrderId{9999999999};
  cancel.position_side = PositionSide::kLong;

  std::string result = encoder_->create_cancel_order_message(cancel);

  EXPECT_TRUE(is_valid_json(result));
  EXPECT_NE(result.find("BTCUSDT"), std::string::npos);
  EXPECT_NE(result.find("LONG"), std::string::npos);
}

TEST_F(WsOeFuturesEncoderTest, CreateCancelOrderMessage_PositionSideIncluded) {
  OrderCancelRequest cancel;
  cancel.symbol = "BTCUSDT";
  cancel.orig_cl_order_id = OrderId{123};
  cancel.cl_order_id = OrderId{456};
  cancel.position_side = PositionSide::kShort;

  std::string result = encoder_->create_cancel_order_message(cancel);

  EXPECT_NE(result.find("positionSide"), std::string::npos);
  EXPECT_NE(result.find("SHORT"), std::string::npos);
}

TEST_F(WsOeFuturesEncoderTest, CreateModifyOrderMessage_ValidRequest_ContainsAllParams) {
  OrderModifyRequest modify;
  modify.symbol = "BTCUSDT";
  modify.orig_client_order_id = OrderId{1111111111};
  modify.side = OrderSide::kBuy;
  modify.order_qty = QtyType::from_double(0.75);
  modify.price = PriceType::from_double(51000.00);
  modify.position_side = PositionSide::kLong;

  std::string result = encoder_->create_modify_order_message(modify);

  EXPECT_TRUE(is_valid_json(result));
  EXPECT_NE(result.find("BTCUSDT"), std::string::npos);
  EXPECT_NE(result.find("LONG"), std::string::npos);
  EXPECT_NE(result.find("order.modify"), std::string::npos);
}

TEST_F(WsOeFuturesEncoderTest, CreateCancelAndReorderMessage_FuturesUsesModify) {
  OrderCancelAndNewOrderSingle replace;
  replace.symbol = "BTCUSDT";
  replace.cl_origin_order_id = OrderId{1111111111};
  replace.cancel_new_order_id = OrderId{2222222222};
  replace.cl_new_order_id = OrderId{3333333333};
  replace.side = OrderSide::kBuy;
  replace.ord_type = OrderType::kLimit;
  replace.order_qty = QtyType::from_double(0.75);
  replace.price = PriceType::from_double(51000.00);
  replace.time_in_force = TimeInForce::kGoodTillCancel;
  replace.self_trade_prevention_mode = SelfTradePreventionMode::kNone;
  replace.position_side = PositionSide::kLong;

  std::string result = encoder_->create_cancel_and_reorder_message(replace);

  EXPECT_TRUE(is_valid_json(result));
  EXPECT_NE(result.find("BTCUSDT"), std::string::npos);
  EXPECT_NE(result.find("order.modify"), std::string::npos);
}

// ============================================================================
// Test with Real Test Data
// ============================================================================

TEST_F(WsOeFuturesEncoderTest, CreateOrderMessage_MatchesTestData) {
  // Using data from test/data/binance_futures/json/request/order_place.json
  NewSingleOrderData order;
  order.symbol = "BTCUSDT";
  order.side = OrderSide::kSell;
  order.ord_type = OrderType::kLimit;
  order.order_qty = QtyType::from_double(0.00112);
  order.price = PriceType::from_double(89671.10);
  order.cl_order_id = OrderId{1765798804108450726};
  order.time_in_force = TimeInForce::kGoodTillCancel;
  order.self_trade_prevention_mode = SelfTradePreventionMode::kExpireTaker;
  order.position_side = PositionSide::kLong;

  std::string result = encoder_->create_order_message(order);

  EXPECT_TRUE(is_valid_json(result));
  EXPECT_NE(result.find("BTCUSDT"), std::string::npos);
  EXPECT_NE(result.find("SELL"), std::string::npos);
  EXPECT_NE(result.find("LIMIT"), std::string::npos);
  EXPECT_NE(result.find("LONG"), std::string::npos);
  EXPECT_NE(result.find("1765798804108450726"), std::string::npos);
}

TEST_F(WsOeFuturesEncoderTest, CreateCancelOrderMessage_MatchesTestData) {
  // Using data from test/data/binance_futures/json/request/order_cancel.json
  OrderCancelRequest cancel;
  cancel.symbol = "BTCUSDT";
  cancel.orig_cl_order_id = OrderId{1765798021226795586};
  cancel.cl_order_id = OrderId{1765798021226795586};

  std::string result = encoder_->create_cancel_order_message(cancel);

  EXPECT_TRUE(is_valid_json(result));
  EXPECT_NE(result.find("BTCUSDT"), std::string::npos);
  EXPECT_NE(result.find("1765798021226795586"), std::string::npos);
}

// ============================================================================
// JSON Structure Validation Tests
// ============================================================================

TEST_F(WsOeFuturesEncoderTest, AllOrderMessages_ProduceValidJson_NoParsingErrors) {
  NewSingleOrderData order;
  order.symbol = "BTCUSDT";
  order.side = OrderSide::kBuy;
  order.ord_type = OrderType::kLimit;
  order.order_qty = QtyType::from_double(1.0);
  order.price = PriceType::from_double(50000.00);
  order.cl_order_id = OrderId{123};
  order.time_in_force = TimeInForce::kGoodTillCancel;
  order.self_trade_prevention_mode = SelfTradePreventionMode::kNone;
  order.position_side = PositionSide::kLong;

  EXPECT_TRUE(is_valid_json(encoder_->create_order_message(order)));

  OrderCancelRequest cancel;
  cancel.symbol = "BTCUSDT";
  cancel.orig_cl_order_id = OrderId{123};
  cancel.cl_order_id = OrderId{456};
  cancel.position_side = PositionSide::kLong;
  EXPECT_TRUE(is_valid_json(encoder_->create_cancel_order_message(cancel)));

  OrderModifyRequest modify;
  modify.symbol = "BTCUSDT";
  modify.orig_client_order_id = OrderId{789};
  modify.side = OrderSide::kBuy;
  modify.order_qty = QtyType::from_double(1.0);
  modify.price = PriceType::from_double(50000.00);
  modify.position_side = PositionSide::kLong;
  EXPECT_TRUE(is_valid_json(encoder_->create_modify_order_message(modify)));
}
