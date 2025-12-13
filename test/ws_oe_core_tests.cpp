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

#include "common/logger.h"
#include "websocket/order_entry/exchanges/binance/spot/binance_spot_oe_traits.h"
#include "websocket/order_entry/spot_ws_oe_decoder.h"

using namespace core;
using namespace common;

class WsOeDecoderTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    logger_ = std::make_unique<Logger>();
    logger_->setLevel(LogLevel::kDebug);
    logger_->clearSink();

    // Store producer persistently before passing to decoder
    producer_ = std::make_unique<Logger::Producer>(logger_->make_producer());
    decoder_ = std::make_unique<SpotWsOeDecoder>(*producer_);
  }

  static void TearDownTestSuite() {
    decoder_.reset();
    producer_.reset();
    logger_->shutdown();
    logger_.reset();
  }

  static std::unique_ptr<Logger> logger_;
  static std::unique_ptr<Logger::Producer> producer_;
  static std::unique_ptr<SpotWsOeDecoder> decoder_;
};

std::unique_ptr<Logger> WsOeDecoderTest::logger_;
std::unique_ptr<Logger::Producer> WsOeDecoderTest::producer_;
std::unique_ptr<SpotWsOeDecoder> WsOeDecoderTest::decoder_;

// ============================================================================
// Session Response Decode Tests
// ============================================================================

TEST_F(WsOeDecoderTest, DecodeSessionLogonResponse_ValidPayload) {
  const std::string payload = R"({
    "id": "login_1699564800000",
    "status": 200,
    "result": {
      "apiKey": "test_api_key",
      "authorizedSince": 1699564800000,
      "connectedSince": 1699564799000,
      "returnRateLimits": true,
      "serverTime": 1699564800000,
      "userDataStream": true
    },
    "rateLimits": [
      {
        "rateLimitType": "REQUEST_WEIGHT",
        "interval": "MINUTE",
        "intervalNum": 1,
        "limit": 6000,
        "count": 1
      }
    ]
  })";

  auto wire_msg = decoder_->decode(payload);
  ASSERT_FALSE(std::holds_alternative<std::monostate>(wire_msg));
  EXPECT_TRUE(std::holds_alternative<BinanceSpotOeTraits::SessionLogonResponse>(wire_msg));
}

TEST_F(WsOeDecoderTest, DecodeUserDataStreamSubscribe_ValidPayload) {
  const std::string payload = R"({
    "id": "subscribe_1699564800000",
    "status": 200,
    "result": {}
  })";

  auto wire_msg = decoder_->decode(payload);
  ASSERT_FALSE(std::holds_alternative<std::monostate>(wire_msg));
  EXPECT_TRUE(std::holds_alternative<BinanceSpotOeTraits::SessionUserSubscriptionResponse>(wire_msg));
}

TEST_F(WsOeDecoderTest, DecodeUserDataStreamUnsubscribe_ValidPayload) {
  const std::string payload = R"({
    "id": "unsubscribe_1699564800000",
    "status": 200,
    "result": {}
  })";

  auto wire_msg = decoder_->decode(payload);
  ASSERT_FALSE(std::holds_alternative<std::monostate>(wire_msg));
  EXPECT_TRUE(std::holds_alternative<BinanceSpotOeTraits::SessionUserUnsubscriptionResponse>(wire_msg));
}

// ============================================================================
// Execution Report Decode Tests
// ============================================================================

TEST_F(WsOeDecoderTest, DecodeExecutionReport_Trade_ValidPayload) {
  const std::string payload = R"({
    "subscriptionId": 1,
    "event": {
      "e": "executionReport",
      "E": 1699564800000,
      "s": "BTCUSDT",
      "c": "1234567890",
      "S": "BUY",
      "o": "LIMIT",
      "f": "GTC",
      "q": "0.50000",
      "p": "50000.00",
      "P": "0.00",
      "F": "0.00000",
      "g": -1,
      "C": "",
      "x": "TRADE",
      "X": "PARTIALLY_FILLED",
      "r": "NONE",
      "i": 9876543210,
      "l": "0.25000",
      "z": "0.25000",
      "L": "50000.00",
      "n": "0.00125",
      "N": "BTC",
      "T": 1699564800000,
      "t": 555555,
      "v": 0,
      "I": 12345,
      "w": true,
      "m": false,
      "M": false,
      "O": 1699564799000,
      "Z": "12500.00",
      "Y": "12500.00",
      "Q": "0.00",
      "W": 1699564800000,
      "V": "NONE"
    }
  })";

  auto wire_msg = decoder_->decode(payload);
  ASSERT_FALSE(std::holds_alternative<std::monostate>(wire_msg));
  EXPECT_TRUE(std::holds_alternative<BinanceSpotOeTraits::ExecutionReportResponse>(wire_msg));

  const auto& report = std::get<BinanceSpotOeTraits::ExecutionReportResponse>(wire_msg);
  EXPECT_EQ(report.event.symbol, "BTCUSDT");
  EXPECT_EQ(report.event.side, "BUY");
  EXPECT_EQ(report.event.execution_type, "TRADE");
  EXPECT_EQ(report.event.order_status, "PARTIALLY_FILLED");
}

TEST_F(WsOeDecoderTest, DecodeExecutionReport_NewOrder_ValidPayload) {
  const std::string payload = R"({
    "subscriptionId": 1,
    "event": {
      "e": "executionReport",
      "E": 1699564800000,
      "s": "BTCUSDT",
      "c": "9999999999",
      "S": "BUY",
      "o": "LIMIT",
      "f": "GTC",
      "q": "1.00000",
      "p": "50000.00",
      "P": "0.00",
      "F": "0.00000",
      "g": -1,
      "C": "",
      "x": "NEW",
      "X": "NEW",
      "r": "NONE",
      "i": 12345678901,
      "l": "0.00000",
      "z": "0.00000",
      "L": "0.00",
      "n": "0",
      "N": "",
      "T": 1699564800000,
      "t": -1,
      "v": 0,
      "I": 12345,
      "w": true,
      "m": false,
      "M": false,
      "O": 1699564800000,
      "Z": "0.00",
      "Y": "0.00",
      "Q": "0.00",
      "W": 1699564800000,
      "V": "NONE"
    }
  })";

  auto wire_msg = decoder_->decode(payload);
  ASSERT_FALSE(std::holds_alternative<std::monostate>(wire_msg));
  EXPECT_TRUE(std::holds_alternative<BinanceSpotOeTraits::ExecutionReportResponse>(wire_msg));

  const auto& report = std::get<BinanceSpotOeTraits::ExecutionReportResponse>(wire_msg);
  EXPECT_EQ(report.event.execution_type, "NEW");
  EXPECT_EQ(report.event.order_status, "NEW");
}

TEST_F(WsOeDecoderTest, DecodeExecutionReport_Canceled_ValidPayload) {
  const std::string payload = R"({
    "subscriptionId": 1,
    "event": {
      "e": "executionReport",
      "E": 1699564800000,
      "s": "BTCUSDT",
      "c": "1111111111",
      "S": "BUY",
      "o": "LIMIT",
      "f": "GTC",
      "q": "1.00000",
      "p": "50000.00",
      "P": "0.00",
      "F": "0.00000",
      "g": -1,
      "C": "2222222222",
      "x": "CANCELED",
      "X": "CANCELED",
      "r": "NONE",
      "i": 12345678901,
      "l": "0.00000",
      "z": "0.00000",
      "L": "0.00",
      "n": "0",
      "N": "",
      "T": 1699564800000,
      "t": -1,
      "v": 0,
      "I": 12345,
      "w": false,
      "m": false,
      "M": false,
      "O": 1699564800000,
      "Z": "0.00",
      "Y": "0.00",
      "Q": "0.00",
      "W": 1699564800000,
      "V": "NONE"
    }
  })";

  auto wire_msg = decoder_->decode(payload);
  ASSERT_FALSE(std::holds_alternative<std::monostate>(wire_msg));
  EXPECT_TRUE(std::holds_alternative<BinanceSpotOeTraits::ExecutionReportResponse>(wire_msg));

  const auto& report = std::get<BinanceSpotOeTraits::ExecutionReportResponse>(wire_msg);
  EXPECT_EQ(report.event.execution_type, "CANCELED");
  EXPECT_EQ(report.event.order_status, "CANCELED");
}

// ============================================================================
// Order Response Decode Tests
// ============================================================================

TEST_F(WsOeDecoderTest, DecodePlaceOrderResponse_ValidPayload) {
  const std::string payload = R"({
    "id": "orderplace_123456",
    "status": 200,
    "result": {
      "symbol": "BTCUSDT",
      "orderId": 12345,
      "orderListId": -1,
      "clientOrderId": "9999999999",
      "transactTime": 1699564800000,
      "price": "50000.00",
      "origQty": "1.50000",
      "executedQty": "0.00000",
      "cummulativeQuoteQty": "0.00",
      "status": "NEW",
      "timeInForce": "GTC",
      "type": "LIMIT",
      "side": "BUY",
      "selfTradePreventionMode": "NONE"
    },
    "rateLimits": []
  })";

  auto wire_msg = decoder_->decode(payload);
  ASSERT_FALSE(std::holds_alternative<std::monostate>(wire_msg));
  EXPECT_TRUE(std::holds_alternative<BinanceSpotOeTraits::PlaceOrderResponse>(wire_msg));
}

TEST_F(WsOeDecoderTest, DecodeCancelOrderResponse_ValidPayload) {
  const std::string payload = R"({
    "id": "ordercancel_123456",
    "status": 200,
    "result": {
      "symbol": "BTCUSDT",
      "origClientOrderId": "1111111111",
      "orderId": 12345,
      "orderListId": -1,
      "clientOrderId": "2222222222",
      "transactTime": 1699564800001,
      "price": "50000.00",
      "origQty": "1.50000",
      "executedQty": "0.00000",
      "cummulativeQuoteQty": "0.00",
      "status": "CANCELED",
      "timeInForce": "GTC",
      "type": "LIMIT",
      "side": "BUY",
      "selfTradePreventionMode": "NONE"
    },
    "rateLimits": []
  })";

  auto wire_msg = decoder_->decode(payload);
  ASSERT_FALSE(std::holds_alternative<std::monostate>(wire_msg));
  EXPECT_TRUE(std::holds_alternative<BinanceSpotOeTraits::CancelOrderResponse>(wire_msg));
}

TEST_F(WsOeDecoderTest, DecodeCancelAndReplaceOrderResponse_ValidPayload) {
  const std::string payload = R"({
    "id": "orderreplace_3333333333",
    "status": 200,
    "result": {
      "cancelResult": "SUCCESS",
      "newOrderResult": "SUCCESS",
      "cancelResponse": {
        "symbol": "BTCUSDT",
        "origClientOrderId": "1111111111",
        "orderId": 12345,
        "orderListId": -1,
        "clientOrderId": "2222222222",
        "transactTime": 1699564800001,
        "price": "50000.00",
        "origQty": "1.50000",
        "executedQty": "0.00000",
        "cummulativeQuoteQty": "0.00",
        "status": "CANCELED",
        "timeInForce": "GTC",
        "type": "LIMIT",
        "side": "BUY"
      },
      "newOrderResponse": {
        "symbol": "BTCUSDT",
        "orderId": 12346,
        "orderListId": -1,
        "clientOrderId": "3333333333",
        "transactTime": 1699564800002,
        "price": "51000.00",
        "origQty": "0.60000",
        "executedQty": "0.00000",
        "cummulativeQuoteQty": "0.00",
        "status": "NEW",
        "timeInForce": "GTC",
        "type": "LIMIT",
        "side": "BUY"
      }
    },
    "rateLimits": []
  })";

  auto wire_msg = decoder_->decode(payload);
  ASSERT_FALSE(std::holds_alternative<std::monostate>(wire_msg));
  EXPECT_TRUE(std::holds_alternative<BinanceSpotOeTraits::CancelAndReorderResponse>(wire_msg));
}

TEST_F(WsOeDecoderTest, DecodeCancelAllOrdersResponse_ValidPayload) {
  const std::string payload = R"({
    "id": "ordercancelAll_4444444444",
    "status": 200,
    "result": [
      {
        "symbol": "BTCUSDT",
        "origClientOrderId": "1111111111",
        "orderId": 12345,
        "orderListId": -1,
        "clientOrderId": "5555555555",
        "transactTime": 1699564800001,
        "price": "50000.00",
        "origQty": "1.00000",
        "executedQty": "0.00000",
        "cummulativeQuoteQty": "0.00",
        "status": "CANCELED",
        "timeInForce": "GTC",
        "type": "LIMIT",
        "side": "BUY"
      }
    ],
    "rateLimits": []
  })";

  auto wire_msg = decoder_->decode(payload);
  ASSERT_FALSE(std::holds_alternative<std::monostate>(wire_msg));
  EXPECT_TRUE(std::holds_alternative<BinanceSpotOeTraits::CancelAllOrdersResponse>(wire_msg));
}

// ============================================================================
// Account Update Decode Tests
// ============================================================================

TEST_F(WsOeDecoderTest, DecodeBalanceUpdate_ValidPayload) {
  const std::string payload = R"({
    "subscriptionId": 1,
    "event": {
      "e": "balanceUpdate",
      "E": 1699564800000,
      "a": "BTC",
      "d": "0.00100000",
      "T": 1699564800000
    }
  })";

  auto wire_msg = decoder_->decode(payload);
  ASSERT_FALSE(std::holds_alternative<std::monostate>(wire_msg));
  EXPECT_TRUE(std::holds_alternative<BinanceSpotOeTraits::BalanceUpdateEnvelope>(wire_msg));
}

TEST_F(WsOeDecoderTest, DecodeOutboundAccountPosition_ValidPayload) {
  const std::string payload = R"({
    "subscriptionId": 1,
    "event": {
      "e": "outboundAccountPosition",
      "E": 1699564800000,
      "u": 1699564800000,
      "B": [
        {
          "a": "BTC",
          "f": "1.00000000",
          "l": "0.00000000"
        },
        {
          "a": "USDT",
          "f": "10000.00000000",
          "l": "0.00000000"
        }
      ]
    }
  })";

  auto wire_msg = decoder_->decode(payload);
  ASSERT_FALSE(std::holds_alternative<std::monostate>(wire_msg));
  EXPECT_TRUE(std::holds_alternative<BinanceSpotOeTraits::OutboundAccountPositionEnvelope>(wire_msg));
}

// ============================================================================
// Error Case Tests
// ============================================================================

TEST_F(WsOeDecoderTest, DecodeEmptyPayload_ReturnsMonostate) {
  auto wire_msg = decoder_->decode("");
  EXPECT_TRUE(std::holds_alternative<std::monostate>(wire_msg));
}

TEST_F(WsOeDecoderTest, DecodeInvalidJson_ReturnsMonostate) {
  auto wire_msg = decoder_->decode("{invalid json}");
  EXPECT_TRUE(std::holds_alternative<std::monostate>(wire_msg));
}

TEST_F(WsOeDecoderTest, DecodeApiErrorResponse_ValidPayload) {
  const std::string payload = R"({
    "id": "orderplace_123",
    "status": 400,
    "error": {
      "code": -1102,
      "msg": "Mandatory parameter 'price' was not sent, was empty/null, or malformed."
    }
  })";

  auto wire_msg = decoder_->decode(payload);
  // This should decode as ApiResponse with error
  ASSERT_FALSE(std::holds_alternative<std::monostate>(wire_msg));
}

TEST_F(WsOeDecoderTest, DecodeInsufficientBalance_ValidPayload) {
  const std::string payload = R"({
    "id": "orderplace_123456789",
    "status": 400,
    "error": {
      "code": -2010,
      "msg": "Account has insufficient balance for requested action."
    }
  })";

  auto wire_msg = decoder_->decode(payload);
  ASSERT_FALSE(std::holds_alternative<std::monostate>(wire_msg));
}

