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
#include "websocket/market_data/json_md_decoder.hpp"
#include "websocket/market_data/exchanges/binance/futures/binance_futures_traits.h"
#include "logger.h"

using namespace core;
using namespace common;

namespace futures_test_utils {

std::string load_test_data(const std::string& filename) {
  std::string path = "data/binance_futures/json/request/" + filename;
  std::ifstream file(path);
  if (!file.is_open()) {
    return "";
  }
  return std::string(std::istreambuf_iterator<char>(file),
                     std::istreambuf_iterator<char>());
}

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

template<typename T, typename VariantT>
bool holds_type(const VariantT& var) {
  return std::holds_alternative<T>(var);
}

template<typename T, typename VariantT>
const T& get_or_fail(const VariantT& var, const std::string& context) {
  if (!std::holds_alternative<T>(var)) {
    throw std::runtime_error("Variant does not hold expected type in: " + context);
  }
  return std::get<T>(var);
}

}  // namespace futures_test_utils

using TestFuturesMdDecoder = JsonMdDecoder<BinanceFuturesTraits>;

class WsFuturesMdDecoderTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    logger_ = std::make_unique<Logger>();
    logger_->setLevel(LogLevel::kDebug);
    logger_->clearSink();
    producer_ = std::make_unique<Logger::Producer>(logger_->make_producer());
    decoder_ = std::make_unique<TestFuturesMdDecoder>(*producer_);
  }

  static void TearDownTestSuite() {
    decoder_.reset();
    producer_.reset();
    logger_->shutdown();
    logger_.reset();
  }
  static std::unique_ptr<Logger> logger_;
  static std::unique_ptr<Logger::Producer> producer_;
  static std::unique_ptr<TestFuturesMdDecoder> decoder_;
};
std::unique_ptr<Logger> WsFuturesMdDecoderTest::logger_;
std::unique_ptr<Logger::Producer> WsFuturesMdDecoderTest::producer_;
std::unique_ptr<TestFuturesMdDecoder> WsFuturesMdDecoderTest::decoder_;

TEST_F(WsFuturesMdDecoderTest, DecodeDepthUpdate_RealData_ParsesCorrectly) {
  std::string json = futures_test_utils::load_test_data("depth.json");

  if (json.empty()) {
    GTEST_SKIP() << "futures depth.json not available";
  }

  EXPECT_TRUE(futures_test_utils::is_valid_json(json));

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(futures_test_utils::holds_type<schema::futures::DepthResponse>(wire_msg))
      << "Expected DepthResponse variant type";

  const auto& depth = futures_test_utils::get_or_fail<schema::futures::DepthResponse>(
      wire_msg, "FuturesDecodeDepthUpdate_RealData");

  EXPECT_EQ(depth.stream, "btcusdt@depth");
  EXPECT_EQ(depth.data.event_type, "depthUpdate");
  EXPECT_EQ(depth.data.symbol, "BTCUSDT");
  EXPECT_EQ(depth.data.timestamp, 1765623793768);
  EXPECT_EQ(depth.data.transaction_time, 1765623793767);
  EXPECT_EQ(depth.data.start_update_id, 9446683550081);
  EXPECT_EQ(depth.data.end_update_id, 9446683582696);
  EXPECT_EQ(depth.data.final_update_id_in_last_stream, 9446683550037);

  EXPECT_FALSE(depth.data.bids.empty());
  EXPECT_FALSE(depth.data.asks.empty());

  if (!depth.data.bids.empty()) {
    EXPECT_DOUBLE_EQ(depth.data.bids[0][0], 1000.00);
    EXPECT_DOUBLE_EQ(depth.data.bids[0][1], 28.319);
  }
}

TEST_F(WsFuturesMdDecoderTest, DecodeTradeEvent_RealData_ParsesCorrectly) {
  std::string json = futures_test_utils::load_test_data("trade.json");

  if (json.empty()) {
    GTEST_SKIP() << "futures trade.json not available";
  }

  EXPECT_TRUE(futures_test_utils::is_valid_json(json));

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(futures_test_utils::holds_type<schema::futures::TradeEvent>(wire_msg))
      << "Expected TradeEvent variant type";

  const auto& trade = futures_test_utils::get_or_fail<schema::futures::TradeEvent>(
      wire_msg, "FuturesDecodeTradeEvent_RealData");

  EXPECT_EQ(trade.stream, "btcusdt@aggTrade");
  EXPECT_EQ(trade.data.event_type, "aggTrade");
  EXPECT_EQ(trade.data.symbol, "BTCUSDT");
  EXPECT_EQ(trade.data.event_time, 1765623793856);
  EXPECT_EQ(trade.data.aggregate_trade_id, 3011637835);
  EXPECT_DOUBLE_EQ(trade.data.price, 90558.30);
  EXPECT_DOUBLE_EQ(trade.data.quantity, 0.704);
  EXPECT_EQ(trade.data.first_trade_id, 7007399071);
  EXPECT_EQ(trade.data.last_trade_id, 7007399080);
  EXPECT_EQ(trade.data.trade_time, 1765623793745);
  EXPECT_FALSE(trade.data.is_buyer_market_maker);
}

TEST_F(WsFuturesMdDecoderTest, DecodeSnapshot_RealData_ParsesCorrectly) {
  std::string json = futures_test_utils::load_test_data("snapshot.json");

  if (json.empty()) {
    GTEST_SKIP() << "futures snapshot.json not available";
  }

  EXPECT_TRUE(futures_test_utils::is_valid_json(json));

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(futures_test_utils::holds_type<schema::futures::DepthSnapshot>(wire_msg))
      << "Expected DepthSnapshot variant type";

  const auto& snapshot = futures_test_utils::get_or_fail<schema::futures::DepthSnapshot>(
      wire_msg, "FuturesDecodeSnapshot_RealData");

  EXPECT_EQ(snapshot.id, "snapshot_BTCUSDT");
  EXPECT_EQ(snapshot.status, 200);
  EXPECT_EQ(snapshot.result.book_update_id, 9446683549191);
  EXPECT_EQ(snapshot.result.message_output_time, 1765623793513);
  EXPECT_EQ(snapshot.result.transaction_time, 1765623793506);

  EXPECT_FALSE(snapshot.result.bids.empty());
  EXPECT_FALSE(snapshot.result.asks.empty());

  if (!snapshot.result.bids.empty()) {
    EXPECT_DOUBLE_EQ(snapshot.result.bids[0][0], 90545.20);
    EXPECT_DOUBLE_EQ(snapshot.result.bids[0][1], 0.618);
  }

  if (!snapshot.result.asks.empty()) {
    EXPECT_DOUBLE_EQ(snapshot.result.asks[0][0], 90545.30);
    EXPECT_DOUBLE_EQ(snapshot.result.asks[0][1], 23.955);
  }
}

TEST_F(WsFuturesMdDecoderTest, DecodeDepthUpdate_VerifyPuField) {
  std::string json = futures_test_utils::load_test_data("depth.json");

  if (json.empty()) {
    GTEST_SKIP() << "futures depth.json not available";
  }

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(futures_test_utils::holds_type<schema::futures::DepthResponse>(wire_msg));

  const auto& depth = std::get<schema::futures::DepthResponse>(wire_msg);

  // Verify pu field is properly parsed - this is critical for Futures depth validation
  EXPECT_EQ(depth.data.final_update_id_in_last_stream, 9446683550037);
  EXPECT_LT(depth.data.final_update_id_in_last_stream, depth.data.start_update_id);
}

TEST_F(WsFuturesMdDecoderTest, DecodeDepthUpdate_InlineData_ParsesCorrectly) {
  std::string json = R"({
    "stream":"btcusdt@depth",
    "data":{
      "e":"depthUpdate",
      "E":1234567890000,
      "T":1234567890000,
      "s":"BTCUSDT",
      "U":100,
      "u":110,
      "pu":99,
      "b":[["90000.50","1.5"],["90000.00","2.0"]],
      "a":[["90001.00","1.0"],["90001.50","0.5"]]
    }
  })";

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(futures_test_utils::holds_type<schema::futures::DepthResponse>(wire_msg));

  const auto& depth = futures_test_utils::get_or_fail<schema::futures::DepthResponse>(
      wire_msg, "DecodeDepthUpdate_InlineData");

  EXPECT_EQ(depth.stream, "btcusdt@depth");
  EXPECT_EQ(depth.data.symbol, "BTCUSDT");
  EXPECT_EQ(depth.data.start_update_id, 100);
  EXPECT_EQ(depth.data.end_update_id, 110);
  EXPECT_EQ(depth.data.final_update_id_in_last_stream, 99);
  EXPECT_EQ(depth.data.bids.size(), 2);
  EXPECT_EQ(depth.data.asks.size(), 2);
}

TEST_F(WsFuturesMdDecoderTest, Decode_EmptyPayload_ReturnsMonostate) {
  std::string json = "";

  auto wire_msg = decoder_->decode(json);

  EXPECT_TRUE(futures_test_utils::holds_type<std::monostate>(wire_msg));
}

TEST_F(WsFuturesMdDecoderTest, Decode_InvalidJson_ReturnsMonostate) {
  std::string json = "{invalid json structure}";

  auto wire_msg = decoder_->decode(json);

  EXPECT_TRUE(futures_test_utils::holds_type<std::monostate>(wire_msg));
}
