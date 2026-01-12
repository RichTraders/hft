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
    INI_CONFIG.load("resources/config.ini");
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
