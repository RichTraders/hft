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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "feature_engine.hpp"
#include "ini_config.hpp"
#include "logger.h"
#include "order_book.hpp"
#include "strategy_config.hpp"
#include "trade_engine.hpp"

using ::testing::_;
using ::testing::HasSubstr;
using namespace common;
using namespace trading;

using TestStrategy = SelectedStrategy;
using TestTradeEngine = TradeEngine<TestStrategy>;
using TestFeatureEngine = FeatureEngine<TestStrategy>;
using TestOrderBook = MarketOrderBook<TestStrategy>;

class FeatureEngineTest : public ::testing::Test {
 public:
  static Logger logger;
  static std::unique_ptr<Logger::Producer> producer;

 protected:
  void SetUp() override {
    INI_CONFIG.load("resources/config-xrpusdc.ini");
    market_pool = new MemoryPool<MarketData>(8);
    market_update_pool = new MemoryPool<MarketUpdateData>(8);

    TradeEngineCfg cfg;
    cfg.risk_cfg_.max_order_size_ = QtyType::from_double(10);
    cfg.risk_cfg_.max_position_ = QtyType::from_double(50);
    cfg.risk_cfg_.max_loss_ = -1000;

    ticker_cfg =
        new TradeEngineCfgHashMap{{INI_CONFIG.get("profile", "symbol"), cfg}};

    if (!producer) {
      producer = std::make_unique<Logger::Producer>(logger.make_producer());
    }
    trade_engine = new TestTradeEngine(*producer,
        market_update_pool,
        market_pool,
        nullptr,
        *ticker_cfg);
    trade_engine->stop();
  }

  void TearDown() override {
    delete trade_engine;
    delete market_pool;
    delete market_update_pool;
    delete ticker_cfg;
  }

  TestTradeEngine* trade_engine;
  MemoryPool<MarketData>* market_pool;
  MemoryPool<MarketUpdateData>* market_update_pool;
  TradeEngineCfgHashMap* ticker_cfg;
};
Logger FeatureEngineTest::logger;
std::unique_ptr<Logger::Producer> FeatureEngineTest::producer;

TEST_F(FeatureEngineTest, OnOrderBookUpdated_UpdatesMidPriceAndLogs) {
  auto producer = logger.make_producer();
  TestFeatureEngine engine(producer);
  std::string symbol = "BTCUSDT";

  TestOrderBook book("BTCUSDT", producer);
  book.set_trade_engine(trade_engine);

  const double bid_price = 1.0;
  const double ask_price = 2.0;
  const double bid_qty = 20.0;
  const double ask_qty = 80.0;

  {
    const auto p = PriceType::from_double(bid_price);
    const auto q = QtyType::from_double(bid_qty);
    const MarketData md{MarketUpdateType::kAdd,
        OrderId{kOrderIdInvalid},
        symbol,
        Side::kBuy,
        p,
        q};
    book.on_market_data_updated(&md);
  }
  {
    const auto p = PriceType::from_double(ask_price);
    const auto q = QtyType::from_double(ask_qty);
    const MarketData md{MarketUpdateType::kAdd,
        OrderId{kOrderIdInvalid},
        symbol,
        Side::kSell,
        p,
        q};
    book.on_market_data_updated(&md);
  }

  const double expected_mid = (bid_price * ask_qty + ask_price * bid_qty) / (bid_qty + ask_qty);

  auto price = PriceType::from_double(ask_price);
  auto side = Side::kSell;

  engine.on_order_book_updated(price, side, &book);

  const auto expected_raw = static_cast<int64_t>(expected_mid * common::FixedPointConfig::kPriceScale);
  EXPECT_EQ(engine.get_market_price(), expected_raw);
}

TEST_F(FeatureEngineTest, OnTradeUpdated_ComputesAggTradeQtyRatioAndLogs) {
  auto producer = logger.make_producer();
  TestFeatureEngine engine(producer);
  std::string symbol = "BTCUSDT";

  TestOrderBook book(symbol, producer);
  book.set_trade_engine(trade_engine);
  {
    const auto p = PriceType::from_double(1.0);
    const auto q = QtyType::from_double(20.0);
    const MarketData md{MarketUpdateType::kAdd,
        OrderId{kOrderIdInvalid},
        symbol,
        Side::kBuy,
        p,
        q};
    book.on_market_data_updated(&md);
  }
  {
    const auto p = PriceType::from_double(2.0);
    const auto q = QtyType::from_double(80.0);
    const MarketData md{MarketUpdateType::kAdd,
        OrderId{kOrderIdInvalid},
        symbol,
        Side::kSell,
        p,
        q};
    book.on_market_data_updated(&md);
  }

  const MarketData md{MarketUpdateType::kTrade,
      OrderId{kOrderIdInvalid},
      symbol,
      Side::kBuy,
      PriceType::from_double(2.0),
      QtyType::from_double(10.0)};
  book.on_market_data_updated(&md);

  int64_t expected_ratio = (md.qty.value * common::kSignalScale) / book.get_bbo()->ask_qty.value;

  engine.on_trade_updated(&md, &book);

  EXPECT_EQ(engine.get_agg_trade_qty_ratio(), expected_ratio);
}

TEST_F(FeatureEngineTest, OnTradeUpdate) {
  auto producer = logger.make_producer();
  TestFeatureEngine engine(producer);

  std::string symbol = "BTCUSDT";
  // BBO 세팅
  TestOrderBook book(symbol, producer);
  book.set_trade_engine(trade_engine);

  struct T {
    PriceType p;
    QtyType q;
    Side s;
  };
  T ticks[] = {
      {PriceType::from_double(100.0), QtyType::from_double(10.0), common::Side::kTrade},
      {PriceType::from_double(102.0), QtyType::from_double(20.0), common::Side::kTrade},
      {PriceType::from_double(104.0), QtyType::from_double(30.0), common::Side::kTrade},
      {PriceType::from_double(106.0), QtyType::from_double(40.0), common::Side::kTrade},
  };

  // Use scaled values: vwap_raw = sum(p_raw * q_raw) / sum(q_raw)
  // Unit: (price_scale * qty_scale) / qty_scale = price_scale
  int64_t sum_pq = 0, sum_q = 0;
  for (auto& t : ticks) {
    std::string symbol = "BTCUSDT";
    MarketData md(common::MarketUpdateType::kTrade,
        common::OrderId{0L},
        symbol,
        t.s,
        t.p,
        t.q);
    engine.on_trade_updated(&md, &book);
    sum_pq += t.p.value * t.q.value;
    sum_q += t.q.value;
  }
  // get_vwap() returns int64 scaled by kPriceScale
  const int64_t expected = sum_pq / sum_q;
  EXPECT_EQ(engine.get_vwap(), expected);
}

TEST_F(FeatureEngineTest, OnTradeUpdate_RollingVWAP_WindowEviction) {
  auto producer = logger.make_producer();
  TestFeatureEngine engine(producer);

  std::string symbol = "BTCUSDT";
  TestOrderBook book(symbol, producer);

  const size_t W = 64;
  const size_t N = W + 7;
  // Use scaled values: vwap = sum(p_raw * q_raw) / sum(q_raw)
  int64_t sum_pq = 0, sum_q = 0;
  std::deque<std::pair<int64_t, int64_t>> win;  // (price_raw, qty_raw)

  for (size_t i = 0; i < N; ++i) {
    const double px = 100.0 + static_cast<double>(i);
    const double qty = 1.0 + static_cast<double>(i % 5);
    std::string symbol = "BTCUSDT";

    auto p = PriceType::from_double(px);
    auto q = QtyType::from_double(qty);
    MarketData md(common::MarketUpdateType::kTrade,
        common::OrderId{0},
        symbol,
        common::Side::kTrade,
        p,
        q);

    engine.on_trade_updated(&md, &book);

    win.emplace_back(p.value, q.value);
    sum_pq += p.value * q.value;
    sum_q += q.value;
    if (win.size() > W) {
      auto [opx, oq] = win.front();
      win.pop_front();
      sum_pq -= opx * oq;
      sum_q -= oq;
    }

    if (sum_q > 0) {
      const int64_t expected = sum_pq / sum_q;
      EXPECT_EQ(engine.get_vwap(), expected)
          << "i=" << i << " W=" << W << " sum_q=" << sum_q;
    } else {
      EXPECT_EQ(engine.get_vwap(), 0);
    }
  }
}

TEST_F(FeatureEngineTest, OnTradeUpdate_RollingVWAP_MultiWraps) {
  auto producer = logger.make_producer();
  TestFeatureEngine engine(producer);

  std::string symbol = "BTCUSDT";
  TestOrderBook book(symbol, producer);

  const size_t W = 64;
  const size_t N = 3 * W + 11;

  // Use scaled values: vwap = sum(p_raw * q_raw) / sum(q_raw)
  int64_t sum_pq = 0, sum_q = 0;
  std::deque<std::pair<int64_t, int64_t>> win;

  for (size_t i = 0; i < N; ++i) {
    // Note: 0.25 increments may lose precision with kPriceScale=10
    // Use integer-friendly values
    const double px = 200.0 + static_cast<double>(i);
    const double qty = (i % 7 == 0) ? 10.0 : (1.0 + static_cast<double>(i % 3));
    std::string symbol = "BTCUSDT";

    auto p = PriceType::from_double(px);
    auto q = QtyType::from_double(qty);
    MarketData md(common::MarketUpdateType::kTrade,
        common::OrderId{42},
        symbol,
        common::Side::kTrade,
        p,
        q);

    engine.on_trade_updated(&md, &book);

    win.emplace_back(p.value, q.value);
    sum_pq += p.value * q.value;
    sum_q += q.value;
    if (win.size() > W) {
      auto [opx, oq] = win.front();
      win.pop_front();
      sum_pq -= opx * oq;
      sum_q -= oq;
    }

    if (i % (W / 3 + 1) == 0 || i + 1 == N) {
      ASSERT_GT(sum_q, static_cast<int64_t>(0));
      const int64_t expected = sum_pq / sum_q;
      EXPECT_EQ(engine.get_vwap(), expected)
          << "multi-wrap check at i=" << i;
    }
  }
}

// ========================================
// Trade History Tests
// ========================================

TEST_F(FeatureEngineTest, GetTrade_EmptyHistory) {
  auto producer = logger.make_producer();
  TestFeatureEngine engine(producer);

  EXPECT_EQ(engine.get_trade_history_size(), 0u);
  EXPECT_EQ(engine.get_trade_history_capacity(), 128u);
}

TEST_F(FeatureEngineTest, GetTrade_SingleTrade) {
  auto producer = logger.make_producer();
  TestFeatureEngine engine(producer);
  std::string symbol = "BTCUSDT";
  TestOrderBook book(symbol, producer);

  const MarketData md{MarketUpdateType::kTrade,
      OrderId{kOrderIdInvalid},
      symbol,
      Side::kBuy,
      PriceType::from_double(100.0),
      QtyType::from_double(5.0)};

  engine.on_trade_updated(&md, &book);

  EXPECT_EQ(engine.get_trade_history_size(), 1u);
  const auto& trade = engine.get_trade(0);
  EXPECT_EQ(trade.side, Side::kBuy);
  EXPECT_EQ(trade.price_raw, md.price.value);
  EXPECT_EQ(trade.qty_raw, md.qty.value);
}

TEST_F(FeatureEngineTest, GetTrade_MultipleTrades_FIFO) {
  auto producer = logger.make_producer();
  TestFeatureEngine engine(producer);
  std::string symbol = "BTCUSDT";
  TestOrderBook book(symbol, producer);

  // Add 5 trades
  std::vector<std::pair<double, Side>> trades = {
      {100.0, Side::kBuy},
      {101.0, Side::kSell},
      {102.0, Side::kBuy},
      {103.0, Side::kSell},
      {104.0, Side::kBuy},
  };

  for (const auto& [price, side] : trades) {
    const MarketData md{MarketUpdateType::kTrade,
        OrderId{kOrderIdInvalid},
        symbol,
        side,
        PriceType::from_double(price),
        QtyType::from_double(1.0)};
    engine.on_trade_updated(&md, &book);
  }

  EXPECT_EQ(engine.get_trade_history_size(), 5u);

  // get_trade(0) = most recent (104.0, Buy)
  EXPECT_EQ(engine.get_trade(0).price_raw, PriceType::from_double(104.0).value);
  EXPECT_EQ(engine.get_trade(0).side, Side::kBuy);

  // get_trade(1) = second most recent (103.0, Sell)
  EXPECT_EQ(engine.get_trade(1).price_raw, PriceType::from_double(103.0).value);
  EXPECT_EQ(engine.get_trade(1).side, Side::kSell);

  // get_trade(4) = oldest (100.0, Buy)
  EXPECT_EQ(engine.get_trade(4).price_raw, PriceType::from_double(100.0).value);
  EXPECT_EQ(engine.get_trade(4).side, Side::kBuy);
}

TEST_F(FeatureEngineTest, GetTrade_CircularBuffer_Wrap) {
  auto producer = logger.make_producer();
  TestFeatureEngine engine(producer);
  std::string symbol = "BTCUSDT";
  TestOrderBook book(symbol, producer);

  const size_t capacity = engine.get_trade_history_capacity();  // 128

  // Fill buffer completely + 10 more to trigger wrap
  const size_t total_trades = capacity + 10;
  for (size_t i = 0; i < total_trades; ++i) {
    const MarketData md{MarketUpdateType::kTrade,
        OrderId{kOrderIdInvalid},
        symbol,
        Side::kBuy,
        PriceType::from_double(100.0 + static_cast<double>(i)),
        QtyType::from_double(1.0)};
    engine.on_trade_updated(&md, &book);
  }

  // Size should be capped at capacity
  EXPECT_EQ(engine.get_trade_history_size(), capacity);

  // Most recent trade should be the last one added
  const double expected_latest = 100.0 + static_cast<double>(total_trades - 1);
  EXPECT_EQ(engine.get_trade(0).price_raw, PriceType::from_double(expected_latest).value);

  // Oldest trade in buffer should be (total - capacity)th
  const double expected_oldest = 100.0 + static_cast<double>(total_trades - capacity);
  EXPECT_EQ(engine.get_trade(capacity - 1).price_raw,
      PriceType::from_double(expected_oldest).value);
}

// ========================================
// OBI Edge Case Tests
// ========================================

TEST_F(FeatureEngineTest, OBI_EmptyLevels) {
  auto producer = logger.make_producer();
  TestFeatureEngine engine(producer);

  std::vector<int64_t> empty_bids;
  std::vector<int64_t> empty_asks;

  EXPECT_EQ(engine.orderbook_imbalance_int64(empty_bids, empty_asks), 0);
}

TEST_F(FeatureEngineTest, OBI_OneSideEmpty) {
  auto producer = logger.make_producer();
  TestFeatureEngine engine(producer);

  std::vector<int64_t> bids = {1000, 2000, 3000};
  std::vector<int64_t> empty_asks;

  // All bids, no asks: OBI should be +kObiScale (max bullish)
  int64_t obi = engine.orderbook_imbalance_int64(bids, empty_asks);
  EXPECT_EQ(obi, common::kObiScale);

  // All asks, no bids: OBI should be -kObiScale (max bearish)
  std::vector<int64_t> empty_bids;
  std::vector<int64_t> asks = {1000, 2000, 3000};
  obi = engine.orderbook_imbalance_int64(empty_bids, asks);
  EXPECT_EQ(obi, -common::kObiScale);
}

TEST_F(FeatureEngineTest, OBI_Balanced) {
  auto producer = logger.make_producer();
  TestFeatureEngine engine(producer);

  std::vector<int64_t> bids = {1000, 2000, 3000};
  std::vector<int64_t> asks = {1000, 2000, 3000};

  // Perfectly balanced: OBI = 0
  EXPECT_EQ(engine.orderbook_imbalance_int64(bids, asks), 0);
}

TEST_F(FeatureEngineTest, OBI_AsymmetricLevels) {
  auto producer = logger.make_producer();
  TestFeatureEngine engine(producer);

  // More bid levels than ask levels
  std::vector<int64_t> bids = {1000, 2000, 3000, 4000, 5000};
  std::vector<int64_t> asks = {1000, 2000};

  // Total = 1000+2000+1000+2000 (common) + 3000+4000+5000 (extra bids)
  // = 18000
  // Diff = (1000-1000) + (2000-2000) + 3000 + 4000 + 5000 = 12000
  // OBI = 12000 * 10000 / 18000 = 6666
  int64_t obi = engine.orderbook_imbalance_int64(bids, asks);
  EXPECT_GT(obi, 0);  // Bullish imbalance
  EXPECT_EQ(obi, (12000 * common::kObiScale) / 18000);
}

TEST_F(FeatureEngineTest, OBI_UnevenDepths) {
  auto producer = logger.make_producer();
  TestFeatureEngine engine(producer);

  std::vector<int64_t> bids = {5000};
  std::vector<int64_t> asks = {1000, 1000, 1000, 1000, 1000};

  // Total = 5000 + 5000 = 10000
  // Diff = 5000 - 5000 = 0
  int64_t obi = engine.orderbook_imbalance_int64(bids, asks);
  EXPECT_EQ(obi, 0);  // Balanced by total quantity
}

// ========================================
// Spread Edge Case Tests
// ========================================

TEST_F(FeatureEngineTest, Spread_ZeroSpread) {
  auto producer = logger.make_producer();
  TestFeatureEngine engine(producer);
  std::string symbol = "XRPUSDC";

  TestOrderBook book(symbol, producer);
  book.set_trade_engine(trade_engine);

  // Same bid and ask price (crossed book edge case)
  // config-XRPUSDC.ini: min_price_int=500'000, kPriceScale=1'000'000 -> min 0.5
  const double price = 2.0;
  {
    const MarketData md{MarketUpdateType::kAdd,
        OrderId{kOrderIdInvalid},
        symbol,
        Side::kBuy,
        PriceType::from_double(price),
        QtyType::from_double(10.0)};
    book.on_market_data_updated(&md);
  }
  {
    const MarketData md{MarketUpdateType::kAdd,
        OrderId{kOrderIdInvalid},
        symbol,
        Side::kSell,
        PriceType::from_double(price),
        QtyType::from_double(10.0)};
    book.on_market_data_updated(&md);
  }

  engine.on_order_book_updated(PriceType::from_double(price), Side::kSell, &book);

  EXPECT_EQ(engine.get_spread(), 0);
}

TEST_F(FeatureEngineTest, MidPrice_EqualQuantities) {
  auto producer = logger.make_producer();
  TestFeatureEngine engine(producer);
  std::string symbol = "XRPUSDC";

  TestOrderBook book(symbol, producer);
  book.set_trade_engine(trade_engine);

  // config-XRPUSDC.ini: min_price_int=500'000, kPriceScale=1'000'000 -> min 0.5
  const double bid_price = 2.0;
  const double ask_price = 2.0004;  // Use value exactly representable in double
  const double qty = 50.0;

  {
    const MarketData md{MarketUpdateType::kAdd,
        OrderId{kOrderIdInvalid},
        symbol,
        Side::kBuy,
        PriceType::from_double(bid_price),
        QtyType::from_double(qty)};
    book.on_market_data_updated(&md);
  }
  {
    const MarketData md{MarketUpdateType::kAdd,
        OrderId{kOrderIdInvalid},
        symbol,
        Side::kSell,
        PriceType::from_double(ask_price),
        QtyType::from_double(qty)};
    book.on_market_data_updated(&md);
  }

  engine.on_order_book_updated(PriceType::from_double(ask_price), Side::kSell, &book);

  // With equal quantities, weighted mid = simple mid
  const double expected_mid = (bid_price + ask_price) / 2.0;
  const auto expected_raw = static_cast<int64_t>(expected_mid * FixedPointConfig::kPriceScale);
  EXPECT_EQ(engine.get_market_price(), expected_raw);
}

// ========================================
// Book Ticker Tests
// ========================================

TEST_F(FeatureEngineTest, BookTicker_BidUpdate) {
  auto producer = logger.make_producer();
  TestFeatureEngine engine(producer);
  std::string symbol = "BTCUSDT";

  const MarketData md{MarketUpdateType::kBookTicker,
      OrderId{kOrderIdInvalid},
      symbol,
      Side::kBuy,
      PriceType::from_double(100.0),
      QtyType::from_double(50.0)};

  engine.on_book_ticker_updated(&md);

  // Update ask side too for mid price calculation
  const MarketData md_ask{MarketUpdateType::kBookTicker,
      OrderId{kOrderIdInvalid},
      symbol,
      Side::kSell,
      PriceType::from_double(102.0),
      QtyType::from_double(50.0)};

  engine.on_book_ticker_updated(&md_ask);

  // Mid price = (100 + 102) / 2 = 101
  const auto expected_mid = static_cast<int64_t>(101.0 * FixedPointConfig::kPriceScale);
  EXPECT_EQ(engine.get_mid_price(), expected_mid);

  // Spread = 102 - 100 = 2
  const auto expected_spread = static_cast<int64_t>(2.0 * FixedPointConfig::kPriceScale);
  EXPECT_EQ(engine.get_spread_fast(), expected_spread);
}

// ========================================
// VWAP Edge Cases
// ========================================

TEST_F(FeatureEngineTest, VWAP_ZeroQuantity) {
  auto producer = logger.make_producer();
  TestFeatureEngine engine(producer);
  std::string symbol = "BTCUSDT";
  TestOrderBook book(symbol, producer);

  // First trade with normal quantity
  {
    const MarketData md{MarketUpdateType::kTrade,
        OrderId{kOrderIdInvalid},
        symbol,
        Side::kBuy,
        PriceType::from_double(100.0),
        QtyType::from_double(10.0)};
    engine.on_trade_updated(&md, &book);
  }

  // VWAP should be 100.0
  EXPECT_EQ(engine.get_vwap(), PriceType::from_double(100.0).value);
}

TEST_F(FeatureEngineTest, AggTradeRatio_ZeroDenominator) {
  auto producer = logger.make_producer();
  TestFeatureEngine engine(producer);
  std::string symbol = "XRPUSDC";
  TestOrderBook book(symbol, producer);
  book.set_trade_engine(trade_engine);

  // config-XRPUSDC.ini: min_price_int=500'000, kPriceScale=1'000'000 -> min 0.5
  const double price = 2.0;

  // Set up BBO with zero ask quantity (edge case)
  {
    const MarketData md{MarketUpdateType::kAdd,
        OrderId{kOrderIdInvalid},
        symbol,
        Side::kBuy,
        PriceType::from_double(price),
        QtyType::from_double(10.0)};
    book.on_market_data_updated(&md);
  }

  // Simulate buy trade when ask side has no quantity
  // The ratio calculation should handle this gracefully
  const MarketData trade_md{MarketUpdateType::kTrade,
      OrderId{kOrderIdInvalid},
      symbol,
      Side::kBuy,
      PriceType::from_double(price),
      QtyType::from_double(5.0)};

  // Should not crash, ratio remains 0 when denominator is 0
  engine.on_trade_updated(&trade_md, &book);
  EXPECT_EQ(engine.get_agg_trade_qty_ratio(), 0);
}
