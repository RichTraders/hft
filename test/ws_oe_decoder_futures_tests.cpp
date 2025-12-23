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
#include <fstream>
#include "logger.h"
#include "websocket/order_entry/exchanges/binance/futures/futures_ws_oe_decoder.h"
#include "websocket/order_entry/exchanges/binance/futures/binance_futures_oe_traits.h"

using namespace core;
using namespace common;

namespace test_utils {

std::string load_test_data(const std::string& filename) {
  std::string path = "data/binance_futures/json/response/" + filename;
  std::ifstream file(path);
  if (!file.is_open()) {
    return "";
  }
  return std::string(std::istreambuf_iterator<char>(file),
      std::istreambuf_iterator<char>());
}

bool is_valid_json(std::string_view json) {
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

template <typename T, typename VariantT>
bool holds_type(const VariantT& var) {
  return std::holds_alternative<T>(var);
}

template <typename T, typename VariantT>
const T& get_or_fail(const VariantT& var, const std::string& context) {
  if (!std::holds_alternative<T>(var)) {
    throw std::runtime_error(
        "Variant does not hold expected type in: " + context);
  }
  return std::get<T>(var);
}

}  // namespace test_utils

class WsOeFuturesDecoderTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    logger_ = std::make_unique<Logger>();
    logger_->setLevel(LogLevel::kDebug);
    logger_->clearSink();
    producer_ = std::make_unique<Logger::Producer>(logger_->make_producer());
    decoder_ = std::make_unique<FuturesWsOeDecoder>(*producer_);
  }

  static void TearDownTestSuite() {
    decoder_.reset();
    producer_.reset();
    logger_->shutdown();
    logger_.reset();
  }
  static std::unique_ptr<Logger> logger_;
  static std::unique_ptr<Logger::Producer> producer_;
  static std::unique_ptr<FuturesWsOeDecoder> decoder_;
};
std::unique_ptr<Logger> WsOeFuturesDecoderTest::logger_;
std::unique_ptr<Logger::Producer> WsOeFuturesDecoderTest::producer_;
std::unique_ptr<FuturesWsOeDecoder> WsOeFuturesDecoderTest::decoder_;

// ============================================================================
// CancelOrderResponse Tests
// ============================================================================

TEST_F(WsOeFuturesDecoderTest, DecodeCancelOrderResponse_Success_AllFieldsPresent) {
  std::string json = test_utils::load_test_data("order_cancel.json");

  if (json.empty()) {
    GTEST_SKIP() << "order_cancel.json not available";
  }

  EXPECT_TRUE(test_utils::is_valid_json(json));

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(
      test_utils::holds_type<BinanceFuturesOeTraits::CancelOrderResponse>(wire_msg))
      << "Expected CancelOrderResponse variant type";

  const auto& response =
      test_utils::get_or_fail<BinanceFuturesOeTraits::CancelOrderResponse>(wire_msg,
          "DecodeCancelOrderResponse_Success");

  EXPECT_EQ(response.id, "c1766047413582315740");
  EXPECT_EQ(response.status, 200);

  // Verify result fields
  EXPECT_EQ(response.result.order_id, 6268955580);
  EXPECT_EQ(response.result.symbol, "XRPUSDC");
  EXPECT_EQ(response.result.status, "CANCELED");
  EXPECT_EQ(response.result.client_order_id, "1766047413582315740");
  EXPECT_DOUBLE_EQ(response.result.price, 1.8457);
  EXPECT_DOUBLE_EQ(response.result.orig_qty, 3.0);
  EXPECT_DOUBLE_EQ(response.result.executed_qty, 0.0);
  EXPECT_DOUBLE_EQ(response.result.cum_qty, 0.0);
  EXPECT_DOUBLE_EQ(response.result.cum_quote, 0.0);
  EXPECT_EQ(response.result.time_in_force, "GTC");
  EXPECT_EQ(response.result.type, "LIMIT");
  EXPECT_FALSE(response.result.reduce_only);
  EXPECT_FALSE(response.result.close_position);
  EXPECT_EQ(response.result.side, "BUY");
  EXPECT_EQ(response.result.position_side, "LONG");
  EXPECT_DOUBLE_EQ(response.result.stop_price, 0.0);
  EXPECT_EQ(response.result.working_type, "CONTRACT_PRICE");
  EXPECT_FALSE(response.result.price_protect);
  EXPECT_EQ(response.result.orig_type, "LIMIT");
  EXPECT_EQ(response.result.price_match, "NONE");
  EXPECT_EQ(response.result.self_trade_prevention_mode, "EXPIRE_TAKER");
  EXPECT_EQ(response.result.good_till_date, 0);
  EXPECT_EQ(response.result.update_time, 1766047420530);
}

TEST_F(WsOeFuturesDecoderTest, DecodeCancelOrderResponse_InlineJson_Success) {
  std::string json = R"({"id":"c1234567890","status":200,"result":{"orderId":12345,"symbol":"BTCUSDT","status":"CANCELED","clientOrderId":"1234567890","price":"50000.00","avgPrice":"0.00","origQty":"0.001","executedQty":"0.0","cumQty":"0.0","cumQuote":"0.00","timeInForce":"GTC","type":"LIMIT","reduceOnly":false,"closePosition":false,"side":"BUY","positionSide":"LONG","stopPrice":"0.0","workingType":"CONTRACT_PRICE","priceProtect":false,"origType":"LIMIT","priceMatch":"NONE","selfTradePreventionMode":"NONE","goodTillDate":0,"updateTime":1699564800000}})";

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(
      test_utils::holds_type<BinanceFuturesOeTraits::CancelOrderResponse>(wire_msg));

  const auto& response =
      test_utils::get_or_fail<BinanceFuturesOeTraits::CancelOrderResponse>(wire_msg,
          "DecodeCancelOrderResponse_InlineJson");

  EXPECT_EQ(response.status, 200);
  EXPECT_EQ(response.result.symbol, "BTCUSDT");
  EXPECT_EQ(response.result.status, "CANCELED");
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST_F(WsOeFuturesDecoderTest, Decode_EmptyPayload_ReturnsMonostate) {
  std::string json = "";

  auto wire_msg = decoder_->decode(json);

  EXPECT_TRUE(test_utils::holds_type<std::monostate>(wire_msg));
}

TEST_F(WsOeFuturesDecoderTest, Decode_InvalidJson_ReturnsMonostate) {
  std::string json = "{invalid json structure}";

  auto wire_msg = decoder_->decode(json);

  EXPECT_TRUE(test_utils::holds_type<std::monostate>(wire_msg));
}

// ============================================================================
// PlaceOrderResponse Tests
// ============================================================================

TEST_F(WsOeFuturesDecoderTest, DecodePlaceOrderResponse_FromFile_Success) {
  std::string json = test_utils::load_test_data("order_place.json");

  if (json.empty()) {
    GTEST_SKIP() << "order_place.json not available";
  }

  EXPECT_TRUE(test_utils::is_valid_json(json));

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(
      test_utils::holds_type<BinanceFuturesOeTraits::PlaceOrderResponse>(wire_msg))
      << "Expected PlaceOrderResponse variant type";

  const auto& response =
      test_utils::get_or_fail<BinanceFuturesOeTraits::PlaceOrderResponse>(wire_msg,
          "DecodePlaceOrderResponse_FromFile_Success");

  EXPECT_EQ(response.id, "p1766038741004077931");
  EXPECT_EQ(response.status, 200);

  // Verify result fields
  ASSERT_TRUE(response.result.has_value());
  EXPECT_EQ(response.result->order_id, 6268036080);
  EXPECT_EQ(response.result->symbol, "XRPUSDC");
  EXPECT_EQ(response.result->status, "NEW");
  EXPECT_EQ(response.result->client_order_id, "1766038741004077931");
  EXPECT_DOUBLE_EQ(response.result->price, 1.8307);
  EXPECT_DOUBLE_EQ(response.result->avg_price, 0.0);
  EXPECT_DOUBLE_EQ(response.result->orig_qty, 3.0);
  EXPECT_DOUBLE_EQ(response.result->executed_qty, 0.0);
  EXPECT_DOUBLE_EQ(response.result->cum_qty, 0.0);
  EXPECT_DOUBLE_EQ(response.result->cum_quote, 0.0);
  EXPECT_EQ(response.result->time_in_force, "GTC");
  EXPECT_EQ(response.result->type, "LIMIT");
  EXPECT_FALSE(response.result->reduce_only);
  EXPECT_FALSE(response.result->close_position);
  EXPECT_EQ(response.result->side, "BUY");
  EXPECT_EQ(response.result->position_side, "LONG");
  EXPECT_EQ(response.result->stop_price, "0.0000");
  EXPECT_EQ(response.result->working_type, "CONTRACT_PRICE");
  EXPECT_FALSE(response.result->price_protect);
  EXPECT_EQ(response.result->orig_type, "LIMIT");
  EXPECT_EQ(response.result->price_match, "NONE");
  EXPECT_EQ(response.result->self_trade_prevention_mode, "EXPIRE_TAKER");
  EXPECT_EQ(response.result->good_till_date, 0);
  EXPECT_EQ(response.result->update_time, 1766038741577);
}

TEST_F(WsOeFuturesDecoderTest, DecodePlaceOrderResponse_InlineJson_Success) {
  std::string json = R"({"id":"p1234567890","status":200,"result":{"orderId":12345,"symbol":"BTCUSDT","status":"NEW","clientOrderId":"1234567890","price":"50000.00","avgPrice":"0.00","origQty":"0.001","executedQty":"0.0","cumQty":"0.0","cumQuote":"0.00","timeInForce":"GTC","type":"LIMIT","reduceOnly":false,"closePosition":false,"side":"BUY","positionSide":"LONG","stopPrice":"0.0","workingType":"CONTRACT_PRICE","priceProtect":false,"origType":"LIMIT","priceMatch":"NONE","selfTradePreventionMode":"NONE","goodTillDate":0,"updateTime":1699564800000}})";

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(
      test_utils::holds_type<BinanceFuturesOeTraits::PlaceOrderResponse>(wire_msg));

  const auto& response =
      test_utils::get_or_fail<BinanceFuturesOeTraits::PlaceOrderResponse>(wire_msg,
          "DecodePlaceOrderResponse_InlineJson_Success");

  EXPECT_EQ(response.status, 200);
  ASSERT_TRUE(response.result.has_value());
  EXPECT_EQ(response.result->symbol, "BTCUSDT");
  EXPECT_EQ(response.result->status, "NEW");
}

// ============================================================================
// SessionLogonResponse Tests
// ============================================================================

TEST_F(WsOeFuturesDecoderTest, DecodeSessionLogon_Success) {
  std::string json = R"({"id":"l1699564800000","status":200,"result":{"apiKey":"test_api_key","authorizedSince":1699564800000,"connectedSince":1699564799000,"returnRateLimits":true,"serverTime":1699564800000}})";

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(
      test_utils::holds_type<BinanceFuturesOeTraits::SessionLogonResponse>(wire_msg));

  const auto& response =
      test_utils::get_or_fail<BinanceFuturesOeTraits::SessionLogonResponse>(wire_msg,
          "DecodeSessionLogon_Success");

  EXPECT_EQ(response.id, "l1699564800000");
  EXPECT_EQ(response.status, 200);
}

// ============================================================================
// ExecutionReportResponse Tests (ORDER_TRADE_UPDATE)
// ============================================================================

TEST_F(WsOeFuturesDecoderTest, DecodeExecutionReport_FromFile_Success) {
  std::string json = test_utils::load_test_data("execution_report.json");

  if (json.empty()) {
    GTEST_SKIP() << "execution_report.json not available";
  }

  EXPECT_TRUE(test_utils::is_valid_json(json));

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(
      test_utils::holds_type<BinanceFuturesOeTraits::ExecutionReportResponse>(wire_msg))
      << "Expected ExecutionReportResponse variant type";

  const auto& response =
      test_utils::get_or_fail<BinanceFuturesOeTraits::ExecutionReportResponse>(wire_msg,
          "DecodeExecutionReport_FromFile_Success");

  EXPECT_EQ(response.event_type, "ORDER_TRADE_UPDATE");
  EXPECT_EQ(response.transaction_time, 1766059108639);
  EXPECT_EQ(response.event_time, 1766059108640);

  // Verify order update info
  const auto& order = response.event;
  EXPECT_EQ(order.symbol, "XRPUSDC");
  EXPECT_EQ(order.client_order_id, 1766059108639);
  EXPECT_EQ(order.side, "BUY");
  EXPECT_EQ(order.order_type, "LIMIT");
  EXPECT_EQ(order.time_in_force, "GTC");
  EXPECT_DOUBLE_EQ(order.order_quantity, 12.3);
  EXPECT_DOUBLE_EQ(order.order_price, 1.8757);
  EXPECT_DOUBLE_EQ(order.average_price, 1.8757);
  EXPECT_DOUBLE_EQ(order.stop_price, 0.0);
  EXPECT_EQ(order.execution_type, "TRADE");
  EXPECT_EQ(order.order_status, "FILLED");
  EXPECT_EQ(order.order_id, 6270171979);
  EXPECT_DOUBLE_EQ(order.last_executed_quantity, 12.3);
  EXPECT_DOUBLE_EQ(order.cumulative_filled_quantity, 12.3);
  EXPECT_DOUBLE_EQ(order.last_filled_price, 1.8757);
  EXPECT_DOUBLE_EQ(order.commission, 0.0);
  EXPECT_EQ(order.commission_asset, "USDC");
  EXPECT_EQ(order.trade_time, 1766059108639);
  EXPECT_EQ(order.trade_id, 159467838);
  EXPECT_TRUE(order.is_maker);
  EXPECT_TRUE(order.is_reduce_only);
  EXPECT_EQ(order.working_type, "CONTRACT_PRICE");
  EXPECT_EQ(order.original_order_type, "LIMIT");
  EXPECT_EQ(order.position_side, "SHORT");
  EXPECT_FALSE(order.is_close_all);
  EXPECT_DOUBLE_EQ(order.realized_profit, -0.05412);
  EXPECT_FALSE(order.price_protection);
  EXPECT_EQ(order.stp_mode, "EXPIRE_MAKER");
  EXPECT_EQ(order.price_match_mode, "NONE");
  EXPECT_EQ(order.gtd_auto_cancel_time, 0);
  EXPECT_EQ(order.reject_reason, "0");
}

TEST_F(WsOeFuturesDecoderTest, DecodeExecutionReport_InlineJson_Success) {
  std::string json = R"({"e":"ORDER_TRADE_UPDATE","T":1699564800000,"E":1699564800001,"o":{"s":"BTCUSDT","c":"1234567890","S":"SELL","o":"MARKET","f":"GTC","q":"0.01","p":"0","ap":"45000.0","sp":"0","x":"TRADE","X":"FILLED","i":123456789,"l":"0.01","z":"0.01","L":"45000.0","n":"0.00045","N":"USDT","T":1699564800000,"t":987654321,"b":"0","a":"0","m":false,"R":false,"wt":"CONTRACT_PRICE","ot":"MARKET","ps":"BOTH","cp":false,"rp":"10.5","pP":false,"si":0,"ss":0,"V":"NONE","pm":"NONE","gtd":0,"er":"0"}})";

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(
      test_utils::holds_type<BinanceFuturesOeTraits::ExecutionReportResponse>(wire_msg));

  const auto& response =
      test_utils::get_or_fail<BinanceFuturesOeTraits::ExecutionReportResponse>(wire_msg,
          "DecodeExecutionReport_InlineJson_Success");

  EXPECT_EQ(response.event_type, "ORDER_TRADE_UPDATE");
  EXPECT_EQ(response.event.symbol, "BTCUSDT");
  EXPECT_EQ(response.event.order_status, "FILLED");
  EXPECT_DOUBLE_EQ(response.event.realized_profit, 10.5);
}

// ============================================================================
// AccountUpdateResponse Tests (ACCOUNT_UPDATE)
// ============================================================================

TEST_F(WsOeFuturesDecoderTest, DecodeAccountUpdate_FromFile_Success) {
  std::string json = test_utils::load_test_data("account_update.json");

  if (json.empty()) {
    GTEST_SKIP() << "account_update.json not available";
  }

  EXPECT_TRUE(test_utils::is_valid_json(json));

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(
      test_utils::holds_type<BinanceFuturesOeTraits::BalanceUpdateEnvelope>(wire_msg))
      << "Expected BalanceUpdateEnvelope (AccountUpdateResponse) variant type";

  const auto& response =
      test_utils::get_or_fail<BinanceFuturesOeTraits::BalanceUpdateEnvelope>(wire_msg,
          "DecodeAccountUpdate_FromFile_Success");

  EXPECT_EQ(response.event_type, "ACCOUNT_UPDATE");
  EXPECT_EQ(response.transaction_time, 1766059108639);
  EXPECT_EQ(response.event_time, 1766059108640);
  EXPECT_EQ(response.data.reason, "ORDER");

  // Verify balances
  ASSERT_EQ(response.data.balances.size(), 1);
  EXPECT_EQ(response.data.balances[0].asset, "USDC");
  EXPECT_DOUBLE_EQ(response.data.balances[0].wallet_balance, 105.6280354);
  EXPECT_DOUBLE_EQ(response.data.balances[0].cross_wallet, 105.6280354);
  EXPECT_DOUBLE_EQ(response.data.balances[0].balance_change, 0.0);

  // Verify positions
  ASSERT_EQ(response.data.positions.size(), 1);
  EXPECT_EQ(response.data.positions[0].symbol, "XRPUSDC");
  EXPECT_DOUBLE_EQ(response.data.positions[0].position_amount, 0.0);
  EXPECT_DOUBLE_EQ(response.data.positions[0].entry_price, 0.0);
  EXPECT_DOUBLE_EQ(response.data.positions[0].cumulative_realized, -0.08725001);
  EXPECT_DOUBLE_EQ(response.data.positions[0].unrealized_pnl, 0.0);
  EXPECT_EQ(response.data.positions[0].margin_type, "cross");
  EXPECT_DOUBLE_EQ(response.data.positions[0].isolated_wallet, 0.0);
  EXPECT_EQ(response.data.positions[0].position_side, "SHORT");
  EXPECT_EQ(response.data.positions[0].margin_asset, "USDC");
  EXPECT_DOUBLE_EQ(response.data.positions[0].break_even_price, 0.0);
}

TEST_F(WsOeFuturesDecoderTest, DecodeAccountUpdate_InlineJson_Success) {
  std::string json = R"({"e":"ACCOUNT_UPDATE","T":1699564800000,"E":1699564800001,"a":{"B":[{"a":"USDT","wb":"1000.50","cw":"950.25","bc":"-50.25"}],"P":[{"s":"BTCUSDT","pa":"0.01","ep":"45000.0","cr":"100.0","up":"50.0","mt":"isolated","iw":"500.0","ps":"LONG","ma":"USDT","bep":"44500.0"}],"m":"DEPOSIT"}})";

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(
      test_utils::holds_type<BinanceFuturesOeTraits::BalanceUpdateEnvelope>(wire_msg));

  const auto& response =
      test_utils::get_or_fail<BinanceFuturesOeTraits::BalanceUpdateEnvelope>(wire_msg,
          "DecodeAccountUpdate_InlineJson_Success");

  EXPECT_EQ(response.event_type, "ACCOUNT_UPDATE");
  EXPECT_EQ(response.data.reason, "DEPOSIT");

  ASSERT_EQ(response.data.balances.size(), 1);
  EXPECT_EQ(response.data.balances[0].asset, "USDT");
  EXPECT_DOUBLE_EQ(response.data.balances[0].wallet_balance, 1000.50);
  EXPECT_DOUBLE_EQ(response.data.balances[0].balance_change, -50.25);

  ASSERT_EQ(response.data.positions.size(), 1);
  EXPECT_EQ(response.data.positions[0].symbol, "BTCUSDT");
  EXPECT_DOUBLE_EQ(response.data.positions[0].position_amount, 0.01);
  EXPECT_EQ(response.data.positions[0].margin_type, "isolated");
  EXPECT_DOUBLE_EQ(response.data.positions[0].isolated_wallet, 500.0);
}
