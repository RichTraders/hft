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
#include "logger.h"
#include "websocket/order_entry/spot_ws_oe_decoder.h"
#include "websocket/schema/spot/response/order.h"
#include "websocket/schema/spot/response/execution_report.h"
#include "websocket/schema/spot/response/session_response.h"
#include "websocket/schema/spot/response/api_response.h"

using namespace core;
using namespace common;

namespace test_utils {

// Load test data from file
std::string load_test_data(const std::string& filename) {
  std::string path = "data/binance_spot/json/execution_reports/" + filename;
  std::ifstream file(path);
  if (!file.is_open()) {
    // Return empty string if file doesn't exist (user hasn't provided data yet)
    return "";
  }
  return std::string(std::istreambuf_iterator<char>(file),
      std::istreambuf_iterator<char>());
}

// Verify JSON is well-formed
bool is_valid_json(std::string_view json) {
  // Simple validation: check if it starts with { or [ and ends with } or ]
  if (json.empty())
    return false;
  auto trimmed = json;
  while (!trimmed.empty() && std::isspace(trimmed.front()))
    trimmed.remove_prefix(1);
  while (!trimmed.empty() && std::isspace(trimmed.back()))
    trimmed.remove_suffix(1);

  if (trimmed.empty())
    return false;

  char first = trimmed.front();
  char last = trimmed.back();

  return (first == '{' && last == '}') || (first == '[' && last == ']');
}

// Variant type checker
template <typename T, typename VariantT>
bool holds_type(const VariantT& var) {
  return std::holds_alternative<T>(var);
}

// Safe variant getter with better error messages
template <typename T, typename VariantT>
const T& get_or_fail(const VariantT& var, const std::string& context) {
  if (!std::holds_alternative<T>(var)) {
    throw std::runtime_error(
        "Variant does not hold expected type in: " + context);
  }
  return std::get<T>(var);
}

}  // namespace test_utils

class WsOeDecoderTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    logger_ = std::make_unique<Logger>();
    logger_->setLevel(LogLevel::kDebug);
    logger_->clearSink();
    producer_ = std::make_unique<Logger::Producer>(logger_->make_producer());
    decoder_ = std::make_unique<SpotWsOeDecoder>(*producer_);
  }

  static void TearDownTestSuite() {
    decoder_.reset();  // Must reset decoder before producer and logger
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
// ExecutionReportResponse Tests
// ============================================================================

TEST_F(WsOeDecoderTest, DecodeExecutionReport_NewOrder_CorrectVariantType) {
  // Sample execution report with NEW status
  std::string json = R"({
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
      "x": "NEW",
      "X": "NEW",
      "r": "NONE",
      "i": 9876543210,
      "l": "0.00000",
      "z": "0.00000",
      "L": "0.00",
      "n": "0.00000",
      "N": null,
      "T": 1699564800000,
      "t": -1,
      "v": 0,
      "I": 12345,
      "w": true,
      "m": false,
      "M": false,
      "O": 1699564799000,
      "Z": "0.00",
      "Y": "0.00",
      "Q": "0.00",
      "W": 1699564800000,
      "V": "NONE"
    }
  })";

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(
      test_utils::holds_type<schema::ExecutionReportResponse>(wire_msg));

  const auto& exec_report =
      test_utils::get_or_fail<schema::ExecutionReportResponse>(wire_msg,
          "DecodeExecutionReport_NewOrder");

  EXPECT_EQ(exec_report.subscription_id, 1);
  EXPECT_EQ(exec_report.event.event_type, "executionReport");
  EXPECT_EQ(exec_report.event.symbol, "BTCUSDT");
  EXPECT_EQ(exec_report.event.client_order_id, 1234567890);
  EXPECT_EQ(exec_report.event.side, "BUY");
  EXPECT_EQ(exec_report.event.order_type, "LIMIT");
  EXPECT_EQ(exec_report.event.execution_type, "NEW");
  EXPECT_EQ(exec_report.event.order_status, "NEW");
}

TEST_F(WsOeDecoderTest, DecodeExecutionReport_TradeExecution_AllFieldsParsed) {
  std::string json = R"({
    "subscriptionId": 2,
    "event": {
      "e": "executionReport",
      "E": 1699564810000,
      "s": "ETHUSDT",
      "c": "9876543210",
      "S": "SELL",
      "o": "MARKET",
      "f": "GTC",
      "q": "1.00000",
      "p": "0.00",
      "P": "0.00",
      "F": "0.00000",
      "g": -1,
      "C": "",
      "x": "TRADE",
      "X": "FILLED",
      "r": "NONE",
      "i": 1111111111,
      "l": "1.00000",
      "z": "1.00000",
      "L": "3000.50",
      "n": "0.00100",
      "N": "USDT",
      "T": 1699564810000,
      "t": 555555,
      "v": 0,
      "I": 67890,
      "w": false,
      "m": true,
      "M": false,
      "O": 1699564809000,
      "Z": "3000.50",
      "Y": "3000.50",
      "Q": "0.00",
      "W": 1699564810000,
      "V": "NONE"
    }
  })";

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(
      test_utils::holds_type<schema::ExecutionReportResponse>(wire_msg));

  const auto& exec_report =
      test_utils::get_or_fail<schema::ExecutionReportResponse>(wire_msg,
          "DecodeExecutionReport_TradeExecution");

  EXPECT_EQ(exec_report.event.execution_type, "TRADE");
  EXPECT_EQ(exec_report.event.order_status, "FILLED");
  EXPECT_DOUBLE_EQ(exec_report.event.cumulative_filled_quantity, 1.0);
  EXPECT_DOUBLE_EQ(exec_report.event.last_executed_price, 3000.50);
  EXPECT_DOUBLE_EQ(exec_report.event.commission_amount, 0.001);
  ASSERT_TRUE(exec_report.event.commission_asset.has_value());
  EXPECT_EQ(exec_report.event.commission_asset.value(), "USDT");
}

TEST_F(WsOeDecoderTest, DecodeExecutionReport_OrderCanceled_StatusCorrect) {
  std::string json = R"({
    "subscriptionId": 3,
    "event": {
      "e": "executionReport",
      "E": 1699564820000,
      "s": "BTCUSDT",
      "c": "5555555555",
      "S": "BUY",
      "o": "LIMIT",
      "f": "GTC",
      "q": "0.25000",
      "p": "49000.00",
      "P": "0.00",
      "F": "0.00000",
      "g": -1,
      "C": "",
      "x": "CANCELED",
      "X": "CANCELED",
      "r": "NONE",
      "i": 2222222222,
      "l": "0.00000",
      "z": "0.00000",
      "L": "0.00",
      "n": "0.00000",
      "N": null,
      "T": 1699564820000,
      "t": -1,
      "v": 0,
      "I": 11111,
      "w": false,
      "m": false,
      "M": false,
      "O": 1699564815000,
      "Z": "0.00",
      "Y": "0.00",
      "Q": "0.00",
      "W": 1699564820000,
      "V": "NONE"
    }
  })";

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(
      test_utils::holds_type<schema::ExecutionReportResponse>(wire_msg));

  const auto& exec_report =
      test_utils::get_or_fail<schema::ExecutionReportResponse>(wire_msg,
          "DecodeExecutionReport_OrderCanceled");

  EXPECT_EQ(exec_report.event.execution_type, "CANCELED");
  EXPECT_EQ(exec_report.event.order_status, "CANCELED");
}

TEST_F(WsOeDecoderTest,
    DecodeExecutionReport_NullCommissionAsset_OptionalHandled) {
  std::string json = R"({
    "subscriptionId": 4,
    "event": {
      "e": "executionReport",
      "E": 1699564800000,
      "s": "BTCUSDT",
      "c": "1111",
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
      "i": 123,
      "l": "0.00000",
      "z": "0.00000",
      "L": "0.00",
      "n": "0.00000",
      "N": null,
      "T": 1699564800000,
      "t": -1,
      "v": 0,
      "I": 456,
      "w": true,
      "m": false,
      "M": false,
      "O": 1699564799000,
      "Z": "0.00",
      "Y": "0.00",
      "Q": "0.00",
      "W": 1699564800000,
      "V": "NONE"
    }
  })";

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(
      test_utils::holds_type<schema::ExecutionReportResponse>(wire_msg));

  const auto& exec_report =
      test_utils::get_or_fail<schema::ExecutionReportResponse>(wire_msg,
          "DecodeExecutionReport_NullCommissionAsset");

  EXPECT_FALSE(exec_report.event.commission_asset.has_value());
}

// ============================================================================
// SessionLogonResponse Tests
// ============================================================================

TEST_F(WsOeDecoderTest, DecodeSessionLogon_Success_AllFieldsPresent) {
  std::string json = R"({
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

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(test_utils::holds_type<schema::SessionLogonResponse>(wire_msg));

  const auto& logon =
      test_utils::get_or_fail<schema::SessionLogonResponse>(wire_msg,
          "DecodeSessionLogon_Success");

  EXPECT_EQ(logon.id, "login_1699564800000");
  EXPECT_EQ(logon.status, 200);
  EXPECT_TRUE(logon.result.has_value());
  EXPECT_EQ(logon.result->api_key, "test_api_key");
  EXPECT_EQ(logon.result->server_time, 1699564800000);
  EXPECT_EQ(logon.rate_limits->size(), 1);
  EXPECT_EQ(logon.rate_limits.value()[0].rate_limit_type, "REQUEST_WEIGHT");
}

// ============================================================================
// Order Response Tests
// ============================================================================

TEST_F(WsOeDecoderTest, DecodePlaceOrderResponse_ACK_MinimalFields) {
  std::string json = R"({
    "id": "orderplace_1764688108000001",
    "status": 200,
    "result": {
      "symbol": "BTCUSDT",
      "orderListId": -1,
      "clientOrderId": "1764688108000001",
      "transactTime": 1699564800000
    },
    "rateLimits": []
  })";

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(test_utils::holds_type<schema::PlaceOrderResponse>(wire_msg));

  const auto& response =
      test_utils::get_or_fail<schema::PlaceOrderResponse>(wire_msg,
          "DecodePlaceOrderResponse_ACK");

  EXPECT_EQ(response.status, 200);
  EXPECT_EQ(response.result->symbol, "BTCUSDT");
  EXPECT_EQ(response.result->client_order_id, "1764688108000001");
}

TEST_F(WsOeDecoderTest, DecodeCancelOrderResponse_Success_AllFieldsPresent) {
  std::string json = R"({
    "id": "ordercancel_1764688108122001",
    "status": 200,
    "result": {
      "symbol": "ETHUSDT",
      "origClientOrderId": "1111111111",
      "orderId": 54321,
      "orderListId": -1,
      "clientOrderId": "2222222222",
      "transactTime": 1699564810000,
      "price": "3000.00",
      "origQty": "1.50000",
      "executedQty": "0.50000",
      "cummulativeQuoteQty": "1500.00",
      "status": "PARTIALLY_FILLED",
      "timeInForce": "GTC",
      "type": "LIMIT",
      "side": "BUY",
      "selfTradePreventionMode": "NONE"
    },
    "rateLimits": []
  })";

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(test_utils::holds_type<schema::CancelOrderResponse>(wire_msg));

  const auto& response =
      test_utils::get_or_fail<schema::CancelOrderResponse>(wire_msg,
          "DecodeCancelOrderResponse_Success");

  EXPECT_EQ(response.status, 200);
  EXPECT_EQ(response.result->symbol, "ETHUSDT");
  EXPECT_EQ(response.result->original_client_order_id, "1111111111");
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST_F(WsOeDecoderTest, Decode_EmptyPayload_ReturnsMonostate) {
  std::string json = "";

  auto wire_msg = decoder_->decode(json);

  EXPECT_TRUE(test_utils::holds_type<std::monostate>(wire_msg));
}

TEST_F(WsOeDecoderTest, Decode_InvalidJson_ReturnsMonostate) {
  std::string json = "{invalid json structure}";

  auto wire_msg = decoder_->decode(json);

  EXPECT_TRUE(test_utils::holds_type<std::monostate>(wire_msg));
}

TEST_F(WsOeDecoderTest, Decode_MissingRequiredField_ReturnsMonostate) {
  std::string json = R"({
    "subscriptionId": 1
  })";

  auto wire_msg = decoder_->decode(json);

  // Should return monostate or ApiResponse since "event" is missing
  EXPECT_TRUE(test_utils::holds_type<std::monostate>(wire_msg) ||
              test_utils::holds_type<schema::ApiResponse>(wire_msg));
}

TEST_F(WsOeDecoderTest, Decode_WrongTypeField_ReturnsMonostate) {
  std::string json = R"({
    "subscriptionId": "not_a_number",
    "event": {
      "e": "executionReport"
    }
  })";

  auto wire_msg = decoder_->decode(json);

  EXPECT_TRUE(test_utils::holds_type<std::monostate>(wire_msg));
}

// ============================================================================
// User-Provided Test Data Tests
// ============================================================================

TEST_F(WsOeDecoderTest, DecodeUserProvidedData_IfAvailable_ParsesCorrectly) {
  // This test will use user-provided JSON files if available
  std::string json = test_utils::load_test_data("execution_report_new.json");

  if (json.empty()) {
    GTEST_SKIP() << "User-provided test data not available yet";
  }

  EXPECT_TRUE(test_utils::is_valid_json(json));

  auto wire_msg = decoder_->decode(json);

  // Should parse to one of the known types, not monostate
  EXPECT_FALSE(test_utils::holds_type<std::monostate>(wire_msg));
}

TEST_F(WsOeDecoderTest, DecodeMultipleUserFiles_AllValid_ParseWithoutErrors) {
  std::vector<std::string> test_files = {"execution_report_new.json",
      "execution_report_trade.json",
      "execution_report_canceled.json",
    "execution_report_parsing_error.json",
      "session_logon_success.json",
      "placeorder_response_ack.json",
      "cancel_reorder_fail.json"};

  int files_tested = 0;

  for (const auto& filename : test_files) {
    std::string json = test_utils::load_test_data(filename);

    if (json.empty()) {
      continue;  // Skip if file not provided
    }

    files_tested++;

    EXPECT_TRUE(test_utils::is_valid_json(json)) << "File: " << filename;

    auto wire_msg = decoder_->decode(json);

    // Should not return monostate for valid files
    EXPECT_FALSE(test_utils::holds_type<std::monostate>(wire_msg))
        << "File: " << filename << " failed to decode";
  }

  if (files_tested == 0) {
    GTEST_SKIP() << "No user-provided test data files available";
  }
}

// ============================================================================
// CancelAndReorder Response Tests
// ============================================================================

TEST_F(WsOeDecoderTest,
    DecodeCancelAndReorderResponse_PartialFail_ErrorParsedCorrectly) {
  std::string json = test_utils::load_test_data("cancel_reorder_fail.json");

  if (json.empty()) {
    GTEST_SKIP() << "cancel_reorder_fail.json not available";
  }

  EXPECT_TRUE(test_utils::is_valid_json(json));

  auto wire_msg = decoder_->decode(json);

  // Debug: Check what type was actually parsed
  if (test_utils::holds_type<std::monostate>(wire_msg)) {
    FAIL() << "Decoded to monostate (parsing failed)";
  } else if (test_utils::holds_type<schema::ApiResponse>(wire_msg)) {
    FAIL() << "Decoded to ApiResponse instead of CancelAndReorderResponse";
  }

  ASSERT_TRUE(
      test_utils::holds_type<schema::CancelAndReorderResponse>(wire_msg))
      << "Expected CancelAndReorderResponse variant type";

  const auto& response =
      test_utils::get_or_fail<schema::CancelAndReorderResponse>(wire_msg,
          "DecodeCancelAndReorderResponse_PartialFail");

  // Verify response header
  EXPECT_EQ(response.id, "orderreplace_1764690263119909563");
  EXPECT_EQ(response.status, 409);

  // Verify error structure exists
  ASSERT_TRUE(response.error.has_value()) << "Error field should be present";
  EXPECT_EQ(response.error->code, -2021);
  EXPECT_EQ(response.error->message, "Order cancel-replace partially failed.");

  // Verify error data structure
  ASSERT_TRUE(response.error->data.has_value())
      << "Error data should be present";
  const auto& error_data = response.error->data.value();

  // Verify cancel and new order results
  EXPECT_EQ(error_data.cancel_result, "SUCCESS");
  EXPECT_EQ(error_data.new_order_result, "FAILURE");

  // Verify cancelResponse details
  const auto& resp = error_data.cancel_response;
  const auto cancel_resp = std::get<schema::CancelSuccess>(resp);
  EXPECT_EQ(cancel_resp.symbol, "BTCUSDT");
  EXPECT_EQ(cancel_resp.orig_client_order_id, "1764690263066988543");
  EXPECT_EQ(cancel_resp.order_id, 53230736388);
  EXPECT_EQ(cancel_resp.order_list_id, -1);
  EXPECT_EQ(cancel_resp.client_order_id, "1764690263119909562");
  EXPECT_EQ(cancel_resp.transact_time, 1764690263200);
  EXPECT_EQ(cancel_resp.price, "90636.16000000");
  EXPECT_EQ(cancel_resp.orig_qty, "0.00006000");
  EXPECT_EQ(cancel_resp.executed_qty, "0.00000000");
  EXPECT_EQ(cancel_resp.cummulative_quote_qty, "0.00000000");
  EXPECT_EQ(cancel_resp.status, "CANCELED");
  EXPECT_EQ(cancel_resp.time_in_force, "GTC");
  EXPECT_EQ(cancel_resp.type, "LIMIT");
  EXPECT_EQ(cancel_resp.side, "BUY");
  EXPECT_EQ(cancel_resp.self_trade_prevention_mode, "EXPIRE_TAKER");
}

TEST_F(WsOeDecoderTest, DecodeCancelAndReorderResponse_CancelFailure) {
  std::string json = test_utils::load_test_data("cancel_reorder_cancel_fail.json");

  if (json.empty()) {
    GTEST_SKIP() << "cancel_reorder_cancel_fail.json not available";
  }

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(
      test_utils::holds_type<schema::CancelAndReorderResponse>(wire_msg));

  const auto& response =
      test_utils::get_or_fail<schema::CancelAndReorderResponse>(wire_msg,
          "DecodeCancelAndReorderResponse_CancelFailure");

  ASSERT_TRUE(response.error.has_value());
  ASSERT_TRUE(response.error->data.has_value());

  const auto& error_data = response.error->data.value();
  EXPECT_EQ(error_data.cancel_result, "FAILURE");
  EXPECT_EQ(error_data.new_order_result, "NOT_ATTEMPTED");

  const auto& resp = error_data.cancel_response;
  const auto cancel_resp = std::get<schema::ShortError>(resp);
  EXPECT_EQ(cancel_resp.code, -2011) << "cancel response code should be -2011";
  EXPECT_EQ(cancel_resp.msg, "Unknown order sent.") << "Expected cancel response message : Unknown order sent.";

  EXPECT_TRUE(std::holds_alternative<std::monostate>(error_data.new_order_response));
}
