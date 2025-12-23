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
#include "core/websocket/market_data/onepass_binance_futures_md_decoder.hpp"

using namespace core;
using namespace common;

namespace onepass_test_utils {

std::string load_test_data(const std::string& filename) {
  std::string path = "data/binance_futures/json/response/" + filename;
  std::ifstream file(path);
  if (!file.is_open()) {
    return "";
  }
  std::string content{std::istreambuf_iterator<char>(file),
                      std::istreambuf_iterator<char>()};
  std::string minified;
  glz::minify_json(content, minified);
  return minified;
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

}  // namespace onepass_test_utils

class OnepassMdDecoderTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    logger_ = std::make_unique<Logger>();
    logger_->setLevel(LogLevel::kDebug);
    logger_->clearSink();
    producer_ = std::make_unique<Logger::Producer>(logger_->make_producer());
    decoder_ = std::make_unique<OnepassBinanceFuturesMdDecoder>(*producer_);
  }

  static void TearDownTestSuite() {
    decoder_.reset();
    producer_.reset();
    logger_->shutdown();
    logger_.reset();
  }
  static std::unique_ptr<Logger> logger_;
  static std::unique_ptr<Logger::Producer> producer_;
  static std::unique_ptr<OnepassBinanceFuturesMdDecoder> decoder_;
};
std::unique_ptr<Logger> OnepassMdDecoderTest::logger_;
std::unique_ptr<Logger::Producer> OnepassMdDecoderTest::producer_;
std::unique_ptr<OnepassBinanceFuturesMdDecoder> OnepassMdDecoderTest::decoder_;

// =============================================================================
// DepthResponse Tests - Only verify fields used by domain converter
// Used: symbol, start_update_id, end_update_id, final_update_id_in_last_stream, bids, asks
// =============================================================================

TEST_F(OnepassMdDecoderTest, DecodeDepth_UsedFields_ParsedCorrectly) {
  std::string json = R"({"stream":"btcusdt@depth","data":{"e":"depthUpdate","E":1234567890123,"T":1234567890124,"s":"BTCUSDT","U":100,"u":200,"pu":99,"b":[["50000.50","1.5"],["49999.00","2.0"]],"a":[["50001.00","0.5"],["50002.50","1.0"]]}})";

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(onepass_test_utils::holds_type<schema::futures::DepthResponse>(wire_msg));
  const auto& depth = onepass_test_utils::get_or_fail<schema::futures::DepthResponse>(wire_msg, "Depth");

  // Verify used fields only
  EXPECT_EQ(depth.data.symbol, "BTCUSDT");
  EXPECT_EQ(depth.data.start_update_id, 100u);
  EXPECT_EQ(depth.data.end_update_id, 200u);
  EXPECT_EQ(depth.data.final_update_id_in_last_stream, 99u);

  ASSERT_EQ(depth.data.bids.size(), 2u);
  EXPECT_DOUBLE_EQ(depth.data.bids[0][0], 50000.50);
  EXPECT_DOUBLE_EQ(depth.data.bids[0][1], 1.5);
  EXPECT_DOUBLE_EQ(depth.data.bids[1][0], 49999.00);
  EXPECT_DOUBLE_EQ(depth.data.bids[1][1], 2.0);

  ASSERT_EQ(depth.data.asks.size(), 2u);
  EXPECT_DOUBLE_EQ(depth.data.asks[0][0], 50001.00);
  EXPECT_DOUBLE_EQ(depth.data.asks[0][1], 0.5);
  EXPECT_DOUBLE_EQ(depth.data.asks[1][0], 50002.50);
  EXPECT_DOUBLE_EQ(depth.data.asks[1][1], 1.0);
}

TEST_F(OnepassMdDecoderTest, DecodeDepth_RealData_ParsedCorrectly) {
  std::string json = onepass_test_utils::load_test_data("depth.json");
  if (json.empty()) {
    GTEST_SKIP() << "depth.json not available";
  }

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(onepass_test_utils::holds_type<schema::futures::DepthResponse>(wire_msg));
  const auto& depth = onepass_test_utils::get_or_fail<schema::futures::DepthResponse>(wire_msg, "Depth");

  // Verify essential fields are non-empty/valid
  EXPECT_FALSE(depth.data.symbol.empty());
  EXPECT_GT(depth.data.end_update_id, 0u);
  EXPECT_FALSE(depth.data.bids.empty());
  EXPECT_FALSE(depth.data.asks.empty());
}

// =============================================================================
// TradeEvent Tests - Only verify fields used by domain converter
// Used: symbol, price, quantity, is_buyer_market_maker
// =============================================================================

TEST_F(OnepassMdDecoderTest, DecodeTrade_UsedFields_ParsedCorrectly) {
  std::string json = R"({"stream":"btcusdt@aggTrade","data":{"e":"aggTrade","E":1234567890123,"a":123456789,"s":"BTCUSDT","p":"50123.45","q":"0.123","f":100000,"l":100005,"T":1234567890124,"m":true}})";

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(onepass_test_utils::holds_type<schema::futures::TradeEvent>(wire_msg));
  const auto& trade = onepass_test_utils::get_or_fail<schema::futures::TradeEvent>(wire_msg, "Trade");

  // Verify used fields only
  EXPECT_EQ(trade.data.symbol, "BTCUSDT");
  EXPECT_DOUBLE_EQ(trade.data.price, 50123.45);
  EXPECT_DOUBLE_EQ(trade.data.quantity, 0.123);
  EXPECT_TRUE(trade.data.is_buyer_market_maker);
}

TEST_F(OnepassMdDecoderTest, DecodeTrade_IsBuyerMarketMakerFalse) {
  std::string json = R"({"stream":"ethusdt@aggTrade","data":{"e":"aggTrade","E":1234567890123,"a":999,"s":"ETHUSDT","p":"2500.00","q":"5.0","f":1,"l":2,"T":1234567890124,"m":false}})";

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(onepass_test_utils::holds_type<schema::futures::TradeEvent>(wire_msg));
  const auto& trade = onepass_test_utils::get_or_fail<schema::futures::TradeEvent>(wire_msg, "Trade");

  EXPECT_EQ(trade.data.symbol, "ETHUSDT");
  EXPECT_DOUBLE_EQ(trade.data.price, 2500.00);
  EXPECT_DOUBLE_EQ(trade.data.quantity, 5.0);
  EXPECT_FALSE(trade.data.is_buyer_market_maker);
}

TEST_F(OnepassMdDecoderTest, DecodeTrade_RealData_ParsedCorrectly) {
  std::string json = onepass_test_utils::load_test_data("trade.json");
  if (json.empty()) {
    GTEST_SKIP() << "trade.json not available";
  }

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(onepass_test_utils::holds_type<schema::futures::TradeEvent>(wire_msg));
  const auto& trade = onepass_test_utils::get_or_fail<schema::futures::TradeEvent>(wire_msg, "Trade");

  EXPECT_FALSE(trade.data.symbol.empty());
  EXPECT_GT(trade.data.price, 0.0);
  EXPECT_GT(trade.data.quantity, 0.0);
}

// =============================================================================
// BookTickerEvent Tests - Only verify fields used by domain converter
// Used: symbol, update_id, best_bid_price, best_bid_qty, best_ask_price, best_ask_qty
// =============================================================================

TEST_F(OnepassMdDecoderTest, DecodeBookTicker_UsedFields_ParsedCorrectly) {
  std::string json = R"({"stream":"btcusdt@bookTicker","data":{"e":"bookTicker","u":123456789,"s":"BTCUSDT","b":"50000.00","B":"10.5","a":"50001.00","A":"5.25","T":1234567890124,"E":1234567890123}})";

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(onepass_test_utils::holds_type<schema::futures::BookTickerEvent>(wire_msg));
  const auto& ticker = onepass_test_utils::get_or_fail<schema::futures::BookTickerEvent>(wire_msg, "BookTicker");

  // Verify used fields only
  EXPECT_EQ(ticker.data.symbol, "BTCUSDT");
  EXPECT_EQ(ticker.data.update_id, 123456789u);
  EXPECT_DOUBLE_EQ(ticker.data.best_bid_price, 50000.00);
  EXPECT_DOUBLE_EQ(ticker.data.best_bid_qty, 10.5);
  EXPECT_DOUBLE_EQ(ticker.data.best_ask_price, 50001.00);
  EXPECT_DOUBLE_EQ(ticker.data.best_ask_qty, 5.25);
}

TEST_F(OnepassMdDecoderTest, DecodeBookTicker_RealData_ParsedCorrectly) {
  std::string json = onepass_test_utils::load_test_data("book_ticker.json");
  if (json.empty()) {
    GTEST_SKIP() << "book_ticker.json not available";
  }

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(onepass_test_utils::holds_type<schema::futures::BookTickerEvent>(wire_msg));
  const auto& ticker = onepass_test_utils::get_or_fail<schema::futures::BookTickerEvent>(wire_msg, "BookTicker");

  EXPECT_FALSE(ticker.data.symbol.empty());
  EXPECT_GT(ticker.data.update_id, 0u);
  EXPECT_GT(ticker.data.best_bid_price, 0.0);
  EXPECT_GT(ticker.data.best_ask_price, 0.0);
}

// =============================================================================
// DepthSnapshot Tests - Only verify fields used by domain converter
// Used: id (for symbol extraction), book_update_id, bids, asks
// =============================================================================

TEST_F(OnepassMdDecoderTest, DecodeSnapshot_UsedFields_ParsedCorrectly) {
  std::string json = R"({"id":"snapshot_BTCUSDT","status":200,"result":{"lastUpdateId":12345678,"E":1234567890123,"T":1234567890124,"bids":[["50000.00","1.0"],["49999.50","2.5"]],"asks":[["50001.00","0.75"],["50002.00","1.25"]]}})";

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(onepass_test_utils::holds_type<schema::futures::DepthSnapshot>(wire_msg));
  const auto& snapshot = onepass_test_utils::get_or_fail<schema::futures::DepthSnapshot>(wire_msg, "Snapshot");

  // Verify used fields only
  EXPECT_EQ(snapshot.id, "snapshot_BTCUSDT");
  EXPECT_EQ(snapshot.result.book_update_id, 12345678u);

  ASSERT_EQ(snapshot.result.bids.size(), 2u);
  EXPECT_DOUBLE_EQ(snapshot.result.bids[0][0], 50000.00);
  EXPECT_DOUBLE_EQ(snapshot.result.bids[0][1], 1.0);
  EXPECT_DOUBLE_EQ(snapshot.result.bids[1][0], 49999.50);
  EXPECT_DOUBLE_EQ(snapshot.result.bids[1][1], 2.5);

  ASSERT_EQ(snapshot.result.asks.size(), 2u);
  EXPECT_DOUBLE_EQ(snapshot.result.asks[0][0], 50001.00);
  EXPECT_DOUBLE_EQ(snapshot.result.asks[0][1], 0.75);
  EXPECT_DOUBLE_EQ(snapshot.result.asks[1][0], 50002.00);
  EXPECT_DOUBLE_EQ(snapshot.result.asks[1][1], 1.25);
}

TEST_F(OnepassMdDecoderTest, DecodeSnapshot_RealData_ParsedCorrectly) {
  std::string json = onepass_test_utils::load_test_data("snapshot.json");
  if (json.empty()) {
    GTEST_SKIP() << "snapshot.json not available";
  }

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(onepass_test_utils::holds_type<schema::futures::DepthSnapshot>(wire_msg));
  const auto& snapshot = onepass_test_utils::get_or_fail<schema::futures::DepthSnapshot>(wire_msg, "Snapshot");

  EXPECT_FALSE(snapshot.id.empty());
  EXPECT_GT(snapshot.result.book_update_id, 0u);
  EXPECT_FALSE(snapshot.result.bids.empty());
  EXPECT_FALSE(snapshot.result.asks.empty());
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_F(OnepassMdDecoderTest, Decode_EmptyPayload_ReturnsMonostate) {
  auto wire_msg = decoder_->decode("");
  EXPECT_TRUE(std::holds_alternative<std::monostate>(wire_msg));
}

TEST_F(OnepassMdDecoderTest, Decode_TooShortPayload_ReturnsMonostate) {
  auto wire_msg = decoder_->decode("{}");
  EXPECT_TRUE(std::holds_alternative<std::monostate>(wire_msg));
}

TEST_F(OnepassMdDecoderTest, DecodeDepth_HighPrecisionPrices_ParsedCorrectly) {
  std::string json = R"({"stream":"btcusdt@depth","data":{"e":"depthUpdate","E":1,"T":1,"s":"BTCUSDT","U":1,"u":2,"pu":0,"b":[["50000.12345678","1.23456789"]],"a":[["50001.87654321","9.87654321"]]}})";

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(onepass_test_utils::holds_type<schema::futures::DepthResponse>(wire_msg));
  const auto& depth = onepass_test_utils::get_or_fail<schema::futures::DepthResponse>(wire_msg, "Depth");

  EXPECT_NEAR(depth.data.bids[0][0], 50000.12345678, 1e-8);
  EXPECT_NEAR(depth.data.bids[0][1], 1.23456789, 1e-8);
  EXPECT_NEAR(depth.data.asks[0][0], 50001.87654321, 1e-8);
  EXPECT_NEAR(depth.data.asks[0][1], 9.87654321, 1e-8);
}

TEST_F(OnepassMdDecoderTest, DecodeDepth_EmptyOrderBook_ParsedCorrectly) {
  std::string json = R"({"stream":"btcusdt@depth","data":{"e":"depthUpdate","E":1,"T":1,"s":"BTCUSDT","U":1,"u":2,"pu":0,"b":[],"a":[]}})";

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(onepass_test_utils::holds_type<schema::futures::DepthResponse>(wire_msg));
  const auto& depth = onepass_test_utils::get_or_fail<schema::futures::DepthResponse>(wire_msg, "Depth");

  EXPECT_EQ(depth.data.symbol, "BTCUSDT");
  EXPECT_TRUE(depth.data.bids.empty());
  EXPECT_TRUE(depth.data.asks.empty());
}

TEST_F(OnepassMdDecoderTest, DecodeBookTicker_LongSymbol_ParsedCorrectly) {
  std::string json = R"({"stream":"1000shibusdt@bookTicker","data":{"e":"bookTicker","u":999,"s":"1000SHIBUSDT","b":"0.01234","B":"1000000.0","a":"0.01235","A":"500000.0","T":1,"E":1}})";

  auto wire_msg = decoder_->decode(json);

  ASSERT_TRUE(onepass_test_utils::holds_type<schema::futures::BookTickerEvent>(wire_msg));
  const auto& ticker = onepass_test_utils::get_or_fail<schema::futures::BookTickerEvent>(wire_msg, "BookTicker");

  EXPECT_EQ(ticker.data.symbol, "1000SHIBUSDT");
  EXPECT_EQ(ticker.data.update_id, 999u);
  EXPECT_DOUBLE_EQ(ticker.data.best_bid_price, 0.01234);
}
