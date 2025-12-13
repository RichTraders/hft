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
#include "common/memory_pool.hpp"
#include "core/market_data.h"
#include "websocket/market_data/ws_md_core.h"
#include "websocket/market_data/json_md_decoder.hpp"
#include "websocket/market_data/exchanges/binance/spot/binance_spot_traits.h"

using namespace core;
using namespace common;

using TestWsMdCore = WsMdCore<BinanceSpotTraits, JsonMdDecoder>;

class WsMdCoreTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    logger_ = std::make_unique<Logger>();
    logger_->setLevel(LogLevel::kDebug);
    logger_->clearSink();

    pool_ = std::make_unique<MemoryPool<MarketData>>(1024);
    core_ = std::make_unique<TestWsMdCore>(logger_.get(), pool_.get());
  }

  static void TearDownTestSuite() {
    core_.reset();
    pool_.reset();
    logger_->shutdown();
    logger_.reset();
  }

  static std::unique_ptr<Logger> logger_;
  static std::unique_ptr<MemoryPool<MarketData>> pool_;
  static std::unique_ptr<TestWsMdCore> core_;
};

std::unique_ptr<Logger> WsMdCoreTest::logger_;
std::unique_ptr<MemoryPool<MarketData>> WsMdCoreTest::pool_;
std::unique_ptr<TestWsMdCore> WsMdCoreTest::core_;

// ============================================================================
// Encoder Tests (subscription message generation)
// ============================================================================

TEST_F(WsMdCoreTest, CreateMarketDataSubscriptionMessage_Subscribe_ValidJson) {
  std::string msg = core_->create_market_data_subscription_message(
      "test_req_1", "5", "BTCUSDT", true);

  ASSERT_FALSE(msg.empty());

  // Verify it's valid JSON
  glz::json_t parsed;
  auto ec = glz::read_json(parsed, msg);
  ASSERT_FALSE(ec) << "Failed to parse JSON: " << msg;

  // Verify method field
  EXPECT_TRUE(parsed.contains("method"));
}

TEST_F(WsMdCoreTest, CreateMarketDataSubscriptionMessage_Unsubscribe_ValidJson) {
  std::string msg = core_->create_market_data_subscription_message(
      "test_req_2", "10", "ETHUSDT", false);

  ASSERT_FALSE(msg.empty());

  glz::json_t parsed;
  auto ec = glz::read_json(parsed, msg);
  ASSERT_FALSE(ec) << "Failed to parse JSON: " << msg;
}

TEST_F(WsMdCoreTest, CreateTradeDataSubscriptionMessage_Subscribe_ValidJson) {
  std::string msg = core_->create_trade_data_subscription_message(
      "trade_req_1", "100", "BTCUSDT", true);

  ASSERT_FALSE(msg.empty());

  glz::json_t parsed;
  auto ec = glz::read_json(parsed, msg);
  ASSERT_FALSE(ec) << "Failed to parse JSON: " << msg;
}

TEST_F(WsMdCoreTest, CreateSnapshotDataSubscriptionMessage_ValidJson) {
  std::string msg = core_->create_snapshot_data_subscription_message(
      "BTCUSDT", "20");

  ASSERT_FALSE(msg.empty());

  glz::json_t parsed;
  auto ec = glz::read_json(parsed, msg);
  ASSERT_FALSE(ec) << "Failed to parse JSON: " << msg;
}

TEST_F(WsMdCoreTest, RequestInstrumentListMessage_WithSymbol_ValidJson) {
  std::string msg = core_->request_instrument_list_message("BTCUSDT");

  ASSERT_FALSE(msg.empty());

  glz::json_t parsed;
  auto ec = glz::read_json(parsed, msg);
  ASSERT_FALSE(ec) << "Failed to parse JSON: " << msg;
}

TEST_F(WsMdCoreTest, RequestInstrumentListMessage_WithoutSymbol_ValidJson) {
  std::string msg = core_->request_instrument_list_message("");

  ASSERT_FALSE(msg.empty());

  glz::json_t parsed;
  auto ec = glz::read_json(parsed, msg);
  ASSERT_FALSE(ec) << "Failed to parse JSON: " << msg;
}

// ============================================================================
// Decode + Domain Mapping Tests
// ============================================================================

TEST_F(WsMdCoreTest, DecodeAndMapDepthUpdate_ValidPayload_ReturnsMarketData) {
  // Sample depth update payload with stream wrapper
  const std::string payload = R"({
    "stream": "btcusdt@depth@100ms",
    "data": {
      "e": "depthUpdate",
      "E": 1609459200000,
      "s": "BTCUSDT",
      "U": 1000001,
      "u": 1000005,
      "b": [["50000.00", "1.5"], ["49999.00", "2.0"]],
      "a": [["50001.00", "0.8"], ["50002.00", "1.2"]]
    }
  })";

  auto wire_msg = core_->decode(payload);
  ASSERT_FALSE(std::holds_alternative<std::monostate>(wire_msg));

  auto market_data = core_->create_market_data_message(wire_msg);
  EXPECT_EQ(market_data.type, MarketDataType::kMarket);
  EXPECT_FALSE(market_data.data.empty());

  // Clean up allocated memory
  for (auto* entry : market_data.data) {
    pool_->deallocate(entry);
  }
}

TEST_F(WsMdCoreTest, DecodeAndMapTradeEvent_ValidPayload_ReturnsTradeData) {
  // Sample trade event with stream wrapper
  const std::string payload = R"({
    "stream": "btcusdt@trade",
    "data": {
      "e": "trade",
      "E": 1609459200000,
      "s": "BTCUSDT",
      "t": 123456789,
      "p": "50000.00",
      "q": "0.5",
      "T": 1609459199999,
      "m": false,
      "M": true
    }
  })";

  auto wire_msg = core_->decode(payload);
  ASSERT_FALSE(std::holds_alternative<std::monostate>(wire_msg));

  auto market_data = core_->create_market_data_message(wire_msg);
  EXPECT_EQ(market_data.type, MarketDataType::kTrade);
  EXPECT_FALSE(market_data.data.empty());

  // Clean up allocated memory
  for (auto* entry : market_data.data) {
    pool_->deallocate(entry);
  }
}

TEST_F(WsMdCoreTest, DecodeEmptyPayload_ReturnsMonostate) {
  auto wire_msg = core_->decode("");
  EXPECT_TRUE(std::holds_alternative<std::monostate>(wire_msg));
}

TEST_F(WsMdCoreTest, DecodeInvalidJson_ReturnsMonostate) {
  auto wire_msg = core_->decode("{invalid json}");
  EXPECT_TRUE(std::holds_alternative<std::monostate>(wire_msg));
}

TEST_F(WsMdCoreTest, CreateSnapshotDataMessage_ValidDepthSnapshot) {
  // Sample snapshot response
  const std::string payload = R"({
    "id": "snapshot_BTCUSDT",
    "status": 200,
    "result": {
      "lastUpdateId": 1000000,
      "bids": [["50000.00", "1.5"], ["49999.00", "2.0"]],
      "asks": [["50001.00", "0.8"], ["50002.00", "1.2"]]
    }
  })";

  auto wire_msg = core_->decode(payload);
  ASSERT_FALSE(std::holds_alternative<std::monostate>(wire_msg));

  auto snapshot_data = core_->create_snapshot_data_message(wire_msg);
  EXPECT_EQ(snapshot_data.type, MarketDataType::kMarket);
  EXPECT_FALSE(snapshot_data.data.empty());

  // Clean up allocated memory
  for (auto* entry : snapshot_data.data) {
    pool_->deallocate(entry);
  }
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST_F(WsMdCoreTest, DecodeDepthUpdate_EmptyBidsAsks_ValidResult) {
  const std::string payload = R"({
    "stream": "btcusdt@depth@100ms",
    "data": {
      "e": "depthUpdate",
      "E": 1609459200000,
      "s": "BTCUSDT",
      "U": 1000001,
      "u": 1000005,
      "b": [],
      "a": []
    }
  })";

  auto wire_msg = core_->decode(payload);
  ASSERT_FALSE(std::holds_alternative<std::monostate>(wire_msg));

  auto market_data = core_->create_market_data_message(wire_msg);
  EXPECT_TRUE(market_data.data.empty());
}

TEST_F(WsMdCoreTest, DecodeUnknownEventType_ReturnsMonostate) {
  const std::string payload = R"({
    "e": "unknownEventType",
    "E": 1609459200000,
    "s": "BTCUSDT"
  })";

  auto wire_msg = core_->decode(payload);
  // Unknown event types should return monostate
  EXPECT_TRUE(std::holds_alternative<std::monostate>(wire_msg));
}

// ============================================================================
// Integration Test: Full Round-Trip
// ============================================================================

TEST_F(WsMdCoreTest, FullRoundTrip_DecodeAndMap_MultipleLevels) {
  // Test with multiple bid/ask levels
  const std::string payload = R"({
    "stream": "btcusdt@depth@100ms",
    "data": {
      "e": "depthUpdate",
      "E": 1609459200000,
      "s": "BTCUSDT",
      "U": 1000001,
      "u": 1000010,
      "b": [
        ["50000.00", "1.0"],
        ["49999.00", "2.0"],
        ["49998.00", "3.0"],
        ["49997.00", "4.0"],
        ["49996.00", "5.0"]
      ],
      "a": [
        ["50001.00", "0.5"],
        ["50002.00", "1.5"],
        ["50003.00", "2.5"],
        ["50004.00", "3.5"],
        ["50005.00", "4.5"]
      ]
    }
  })";

  auto wire_msg = core_->decode(payload);
  ASSERT_FALSE(std::holds_alternative<std::monostate>(wire_msg));

  auto market_data = core_->create_market_data_message(wire_msg);
  EXPECT_EQ(market_data.type, MarketDataType::kMarket);

  // Should have 10 entries (5 bids + 5 asks)
  EXPECT_EQ(market_data.data.size(), 10);

  // Check sequence IDs
  EXPECT_EQ(market_data.start_idx, 1000001);
  EXPECT_EQ(market_data.end_idx, 1000010);

  // Clean up allocated memory
  for (auto* entry : market_data.data) {
    pool_->deallocate(entry);
  }
}
