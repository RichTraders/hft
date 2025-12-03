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
#include <sstream>
#include <glaze/glaze.hpp>
#include "websocket/market_data/ws_md_decoder.h"
#include "websocket/market_data/ws_md_wire_message.h"
#include "logger.h"

using namespace core;
using namespace common;

namespace test_utils {

// Load test data from file
std::string load_test_data(const std::string& filename) {
  std::string path = "data/market_data/" + filename;
  std::ifstream file(path);
  if (!file.is_open()) {
    return "";
  }
  return std::string(std::istreambuf_iterator<char>(file),
                     std::istreambuf_iterator<char>());
}

// Verify JSON is well-formed
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

// Variant type checker
template<typename T, typename VariantT>
bool holds_type(const VariantT& var) {
  return std::holds_alternative<T>(var);
}

// Safe variant getter with better error messages
template<typename T, typename VariantT>
const T& get_or_fail(const VariantT& var, const std::string& context) {
  if (!std::holds_alternative<T>(var)) {
    throw std::runtime_error("Variant does not hold expected type in: " + context);
  }
  return std::get<T>(var);
}

}  // namespace test_utils

class WsMdDecoderTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    logger_ = std::make_unique<Logger>();
    logger_->setLevel(LogLevel::kDebug);
    logger_->clearSink();
    producer_ = std::make_unique<Logger::Producer>(logger_->make_producer());
    decoder_ = std::make_unique<WsMdDecoder>(*producer_);
  }

  static void TearDownTestSuite() {
    decoder_.reset();
    producer_.reset();
    logger_->shutdown();
    logger_.reset();
  }
  static std::unique_ptr<Logger> logger_;
  static std::unique_ptr<Logger::Producer> producer_;
  static std::unique_ptr<WsMdDecoder> decoder_;
};
std::unique_ptr<Logger> WsMdDecoderTest::logger_;
std::unique_ptr<Logger::Producer> WsMdDecoderTest::producer_;
std::unique_ptr<WsMdDecoder> WsMdDecoderTest::decoder_;

// ============================================================================
// DepthResponse Tests
// ============================================================================

TEST_F(WsMdDecoderTest, DecodeDepthUpdate_RealData_ParsesCorrectly) {
  std::string json = test_utils::load_test_data("dpeth.json");

  if (json.empty()) {
    GTEST_SKIP() << "dpeth.json not available";
  }

  EXPECT_TRUE(test_utils::is_valid_json(json));

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(test_utils::holds_type<schema::DepthResponse>(wire_msg))
      << "Expected DepthResponse variant type";

  const auto& depth = test_utils::get_or_fail<schema::DepthResponse>(
      wire_msg, "DecodeDepthUpdate_RealData");

  // Verify stream name
  EXPECT_EQ(depth.stream, "btcusdt@depth@100ms");

  // Verify depth data
  EXPECT_EQ(depth.data.event_type, "depthUpdate");
  EXPECT_EQ(depth.data.symbol, "BTCUSDT");
  EXPECT_EQ(depth.data.timestamp, 1764726892214426);
  EXPECT_EQ(depth.data.start_update_id, 82319053623);
  EXPECT_EQ(depth.data.end_update_id, 82319053633);

  // Verify bids (should have 3 entries based on the file)
  EXPECT_GE(depth.data.bids.size(), 1);
  if (!depth.data.bids.empty()) {
    // First bid: ["92242.52000000","0.00600000"]
    EXPECT_DOUBLE_EQ(depth.data.bids[0][0], 92242.52);
    EXPECT_DOUBLE_EQ(depth.data.bids[0][1], 0.006);
  }

  // Verify asks (should have 6 entries based on the file)
  EXPECT_GE(depth.data.asks.size(), 1);
  if (!depth.data.asks.empty()) {
    // First ask: ["92309.90000000","0.53316000"]
    EXPECT_DOUBLE_EQ(depth.data.asks[0][0], 92309.90);
    EXPECT_DOUBLE_EQ(depth.data.asks[0][1], 0.53316);
  }
}

TEST_F(WsMdDecoderTest, DecodeDepthUpdate_InlineData_ParsesCorrectly) {
  std::string json = R"({
    "stream":"ethusdt@depth@100ms",
    "data":{
      "e":"depthUpdate",
      "E":1234567890000,
      "s":"ETHUSDT",
      "U":100,
      "u":110,
      "b":[["2000.50","1.5"],["2000.00","2.0"]],
      "a":[["2001.00","1.0"],["2001.50","0.5"]]
    }
  })";

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(test_utils::holds_type<schema::DepthResponse>(wire_msg));

  const auto& depth = test_utils::get_or_fail<schema::DepthResponse>(
      wire_msg, "DecodeDepthUpdate_InlineData");

  EXPECT_EQ(depth.stream, "ethusdt@depth@100ms");
  EXPECT_EQ(depth.data.symbol, "ETHUSDT");
  EXPECT_EQ(depth.data.start_update_id, 100);
  EXPECT_EQ(depth.data.end_update_id, 110);
  EXPECT_EQ(depth.data.bids.size(), 2);
  EXPECT_EQ(depth.data.asks.size(), 2);
}

// ============================================================================
// TradeEvent Tests
// ============================================================================

TEST_F(WsMdDecoderTest, DecodeTradeEvent_RealData_ParsesCorrectly) {
  std::string json = test_utils::load_test_data("trade.json");

  if (json.empty()) {
    GTEST_SKIP() << "trade.json not available";
  }

  EXPECT_TRUE(test_utils::is_valid_json(json));

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(test_utils::holds_type<schema::TradeEvent>(wire_msg))
      << "Expected TradeEvent variant type";

  const auto& trade = test_utils::get_or_fail<schema::TradeEvent>(
      wire_msg, "DecodeTradeEvent_RealData");

  // Verify stream name
  EXPECT_EQ(trade.stream, "btcusdt@trade");

  // Verify trade data
  EXPECT_EQ(trade.data.event_type, "trade");
  EXPECT_EQ(trade.data.symbol, "BTCUSDT");
  EXPECT_EQ(trade.data.event_time, 1764726909787430);
  EXPECT_EQ(trade.data.trade_id, 5598892809);
  EXPECT_DOUBLE_EQ(trade.data.price, 92312.34);
  EXPECT_DOUBLE_EQ(trade.data.quantity, 0.00006);
  EXPECT_EQ(trade.data.trade_time, 1764726909785597);
  EXPECT_TRUE(trade.data.is_buyer_market_maker);
  EXPECT_TRUE(trade.data.ignore_flag);
}

TEST_F(WsMdDecoderTest, DecodeTradeEvent_InlineData_ParsesCorrectly) {
  std::string json = R"({
    "stream":"ethusdt@trade",
    "data":{
      "e":"trade",
      "E":1234567890000,
      "s":"ETHUSDT",
      "t":12345,
      "p":"2000.50",
      "q":"1.5",
      "T":1234567890000,
      "m":false,
      "M":true
    }
  })";

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(test_utils::holds_type<schema::TradeEvent>(wire_msg));

  const auto& trade = test_utils::get_or_fail<schema::TradeEvent>(
      wire_msg, "DecodeTradeEvent_InlineData");

  EXPECT_EQ(trade.stream, "ethusdt@trade");
  EXPECT_EQ(trade.data.symbol, "ETHUSDT");
  EXPECT_EQ(trade.data.trade_id, 12345);
  EXPECT_DOUBLE_EQ(trade.data.price, 2000.50);
  EXPECT_DOUBLE_EQ(trade.data.quantity, 1.5);
  EXPECT_FALSE(trade.data.is_buyer_market_maker);
  EXPECT_TRUE(trade.data.ignore_flag);
}

// ============================================================================
// ExchangeInfoResponse Tests
// ============================================================================

TEST_F(WsMdDecoderTest, DecodeExchangeInfo_RealData_ParsesCorrectly) {
  std::string json = test_utils::load_test_data("exchange_info_response.json");

  if (json.empty()) {
    GTEST_SKIP() << "exchange_info_response.json not available";
  }

  EXPECT_TRUE(test_utils::is_valid_json(json));

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(test_utils::holds_type<schema::ExchangeInfoResponse>(wire_msg))
      << "Expected ExchangeInfoResponse variant type";

  const auto& exchange_info = test_utils::get_or_fail<schema::ExchangeInfoResponse>(
      wire_msg, "DecodeExchangeInfo_RealData");

  // Verify basic fields
  EXPECT_EQ(exchange_info.id, "md_exchangeInfo");
  EXPECT_EQ(exchange_info.status, 200);

  // Verify result contains exchange data
  EXPECT_EQ(exchange_info.result.timezone, "UTC");
  EXPECT_EQ(exchange_info.result.server_time, 1764730961182);

  // Verify symbols array is not empty
  ASSERT_FALSE(exchange_info.result.symbols.empty());

  // Verify first symbol is BTCUSDT
  const auto& btc_symbol = exchange_info.result.symbols[0];
  EXPECT_EQ(btc_symbol.symbol, "BTCUSDT");
  EXPECT_EQ(btc_symbol.status, "TRADING");
  EXPECT_EQ(btc_symbol.base_asset, "BTC");
  EXPECT_EQ(btc_symbol.quote_asset, "USDT");
  EXPECT_EQ(btc_symbol.base_asset_precision, 8);
  EXPECT_EQ(btc_symbol.quote_precision, 8);

  // Verify order types
  EXPECT_FALSE(btc_symbol.order_types.empty());
  EXPECT_TRUE(std::find(btc_symbol.order_types.begin(),
                        btc_symbol.order_types.end(),
                        "LIMIT") != btc_symbol.order_types.end());
  EXPECT_TRUE(std::find(btc_symbol.order_types.begin(),
                        btc_symbol.order_types.end(),
                        "MARKET") != btc_symbol.order_types.end());

  // Verify trading flags
  EXPECT_TRUE(btc_symbol.iceberg_allowed);
  EXPECT_TRUE(btc_symbol.oco_allowed);
  EXPECT_TRUE(btc_symbol.cancel_replace_allowed);
  EXPECT_TRUE(btc_symbol.is_spot_trading_allowed);

  // Verify filters array
  EXPECT_FALSE(btc_symbol.filters.empty());

  // Find and verify PRICE_FILTER
  auto price_filter = std::find_if(btc_symbol.filters.begin(),
                                    btc_symbol.filters.end(),
                                    [](const auto& f) {
                                      return f.filter_type == "PRICE_FILTER";
                                    });
  ASSERT_NE(price_filter, btc_symbol.filters.end());
  EXPECT_TRUE(price_filter->min_price.has_value());
  EXPECT_EQ(price_filter->min_price.value(), "0.01000000");
  EXPECT_TRUE(price_filter->max_price.has_value());
  EXPECT_EQ(price_filter->max_price.value(), "1000000.00000000");
  EXPECT_TRUE(price_filter->tick_size.has_value());
  EXPECT_EQ(price_filter->tick_size.value(), "0.01000000");

  // Find and verify LOT_SIZE
  auto lot_filter = std::find_if(btc_symbol.filters.begin(),
                                  btc_symbol.filters.end(),
                                  [](const auto& f) {
                                    return f.filter_type == "LOT_SIZE";
                                  });
  ASSERT_NE(lot_filter, btc_symbol.filters.end());
  EXPECT_TRUE(lot_filter->min_qty.has_value());
  EXPECT_EQ(lot_filter->min_qty.value(), "0.00001000");
  EXPECT_TRUE(lot_filter->max_qty.has_value());
  EXPECT_EQ(lot_filter->max_qty.value(), "9000.00000000");

  // Verify self trade prevention modes
  EXPECT_EQ(btc_symbol.default_self_trade_prevention_mode, "EXPIRE_MAKER");
  EXPECT_FALSE(btc_symbol.allowed_self_trade_prevention_modes.empty());
  EXPECT_TRUE(std::find(btc_symbol.allowed_self_trade_prevention_modes.begin(),
                        btc_symbol.allowed_self_trade_prevention_modes.end(),
                        "EXPIRE_TAKER") != btc_symbol.allowed_self_trade_prevention_modes.end());

  // Verify permissionSets is not empty
  EXPECT_FALSE(btc_symbol.permission_sets.empty());
  if (!btc_symbol.permission_sets.empty()) {
    EXPECT_FALSE(btc_symbol.permission_sets[0].empty());
  }
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST_F(WsMdDecoderTest, Decode_EmptyPayload_ReturnsMonostate) {
  std::string json = "";

  auto wire_msg = decoder_->decode(json);

  EXPECT_TRUE(test_utils::holds_type<std::monostate>(wire_msg));
}

TEST_F(WsMdDecoderTest, Decode_ConnectedMessage_ReturnsMonostate) {
  std::string json = "__CONNECTED__";

  auto wire_msg = decoder_->decode(json);

  EXPECT_TRUE(test_utils::holds_type<std::monostate>(wire_msg));
}

TEST_F(WsMdDecoderTest, Decode_InvalidJson_ReturnsMonostate) {
  std::string json = "{invalid json structure}";

  auto wire_msg = decoder_->decode(json);

  EXPECT_TRUE(test_utils::holds_type<std::monostate>(wire_msg));
}

TEST_F(WsMdDecoderTest, Decode_UnknownStream_ReturnsMonostate) {
  std::string json = R"({"stream":"unknown@stream","data":{}})";

  auto wire_msg = decoder_->decode(json);

  EXPECT_TRUE(test_utils::holds_type<std::monostate>(wire_msg));
}

// ============================================================================
// Multiple Files Test
// ============================================================================

TEST_F(WsMdDecoderTest, DecodeMultipleFiles_AllValid_ParseWithoutErrors) {
  std::vector<std::string> test_files = {
      "dpeth.json",
      "trade.json"
  };

  int files_tested = 0;

  for (const auto& filename : test_files) {
    std::string json = test_utils::load_test_data(filename);

    if (json.empty()) {
      continue;
    }

    files_tested++;

    EXPECT_TRUE(test_utils::is_valid_json(json)) << "File: " << filename;

    auto wire_msg = decoder_->decode(json);

    EXPECT_FALSE(test_utils::holds_type<std::monostate>(wire_msg))
        << "File: " << filename << " failed to decode";
  }

  if (files_tested == 0) {
    GTEST_SKIP() << "No market data test files available";
  }
}
