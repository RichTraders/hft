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
#include "feature_engine.h"

#include "logger.h"
#include "order_book.h"
#include "trade_engine.h"
#include "ini_config.hpp"

using ::testing::_;
using ::testing::HasSubstr;
using namespace common;
using namespace trading;

class FeatureEngineTest : public ::testing::Test {
 protected:
  void SetUp() override {
    INI_CONFIG.load("resources/config.ini");
    market_pool = new MemoryPool<MarketData>(8);
    market_update_pool = new MemoryPool<MarketUpdateData>(8);

    TradeEngineCfg cfg;
    cfg.risk_cfg_.max_order_size_ = Qty{10};
    cfg.risk_cfg_.max_position_ = Qty{50};
    cfg.risk_cfg_.max_loss_ = -1000;

    ticker_cfg = new TradeEngineCfgHashMap{{INI_CONFIG.get("meta", "ticker"), cfg}};

    trade_engine = new TradeEngine(&logger, market_update_pool, market_pool,
                                   nullptr, *ticker_cfg);
    trade_engine->stop();
  }

  void TearDown() override {
    delete trade_engine;
    delete market_pool;
    delete market_update_pool;
  }

  TradeEngine* trade_engine;
  Logger logger;
  MemoryPool<MarketData>* market_pool;
  MemoryPool<MarketUpdateData>* market_update_pool;
  TradeEngineCfgHashMap* ticker_cfg;
};

TEST_F(FeatureEngineTest, OnOrderBookUpdated_UpdatesMidPriceAndLogs) {

  FeatureEngine engine(&logger);
  std::string symbol = "ETHUSDT";
  // BBO 세팅
  MarketOrderBook book("ETHUSDT", &logger);
  book.set_trade_engine(trade_engine);
  {
    const Price p = Price{100'000.};
    const Qty q{20.0};
    const MarketData md{MarketUpdateType::kAdd,
                        OrderId{kOrderIdInvalid},
                        symbol,
                        Side::kBuy,
                        p,
                        q};
    book.on_market_data_updated(&md);
  }
  {
    const Price p = Price{200'000.};
    const Qty q{80.0};
    const MarketData md{MarketUpdateType::kAdd,
                        OrderId{kOrderIdInvalid},
                        symbol,
                        Side::kSell,
                        p,
                        q};
    book.on_market_data_updated(&md);
  }

  double expected_mid = (100'000 * 80. + 200'000 * 20.) / (20.0 + 80.0);

  auto price = Price{200'000};
  auto side = Side::kSell;

  engine.on_order_book_updated(price, side, &book);

  EXPECT_DOUBLE_EQ(engine.get_mid_price(), expected_mid);
}

TEST_F(FeatureEngineTest, OnTradeUpdated_ComputesAggTradeQtyRatioAndLogs) {
  FeatureEngine engine(&logger);

  std::string symbol = "ETHUSDT";
  // BBO 세팅
  MarketOrderBook book(symbol, &logger);
  book.set_trade_engine(trade_engine);
  {
    const Price p = Price{100'000.};
    const Qty q{20.0};
    const MarketData md{MarketUpdateType::kAdd,
                        OrderId{kOrderIdInvalid},
                        symbol,
                        Side::kBuy,
                        p,
                        q};
    book.on_market_data_updated(&md);
  }
  {
    const Price p = Price{200'000.};
    const Qty q{80.0};
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
                      Price{200'000},
                      Qty{10.0}};
  book.on_market_data_updated(&md);

  double expected_ratio = md.qty.value / book.get_bbo()->ask_qty.value;

  engine.on_trade_updated(&md, &book);

  EXPECT_DOUBLE_EQ(engine.get_agg_trade_qty_ratio(), expected_ratio);
}

TEST_F(FeatureEngineTest, OnTradeUpdate) {
  FeatureEngine engine(&logger);

  std::string symbol = "ETHUSDT";
  // BBO 세팅
  MarketOrderBook book(symbol, &logger);
  book.set_trade_engine(trade_engine);

  struct T {
    Price p;
    Qty q;
    Side s;
  };
  T ticks[] = {
      {Price{100.0}, Qty{10.0}, common::Side::kTrade},
      {Price{102.0}, Qty{20.0}, common::Side::kTrade},
      {Price{104.0}, Qty{30.0}, common::Side::kTrade},
      {Price{106.0}, Qty{40.0}, common::Side::kTrade},
  };

  double sum_pq = 0.0, sum_q = 0.0;
  for (auto& t : ticks) {
    MarketData md(common::MarketUpdateType::kTrade, common::OrderId{0L}, symbol,
                  t.s, t.p, t.q);
    engine.on_trade_updated(&md, &book);
    sum_pq += t.p.value * t.q.value;
    sum_q += t.q.value;
  }
  const double expected = sum_pq / sum_q;
  EXPECT_FLOAT_EQ(engine.get_vwap(), expected);
}

TEST_F(FeatureEngineTest, OnTradeUpdate_RollingVWAP_WindowEviction)
{
  FeatureEngine engine(&logger);

  std::string symbol = "ETHUSDT";
  MarketOrderBook book(symbol, &logger);

  const size_t W = kVwapSize;
  const size_t N = W + 7;
  double sum_pq = 0.0, sum_q = 0.0;
  std::deque<std::pair<double,double>> win;      // (price, qty)

  for (size_t i = 0; i < N; ++i) {
    const double px  = 100.0 + static_cast<double>(i);
    const double qty = 1.0  + static_cast<double>(i % 5);

    MarketData md(common::MarketUpdateType::kTrade,
                  common::OrderId{0},
                  symbol,
                  common::Side::kTrade,
                  Price{px},
                  Qty{qty});

    engine.on_trade_updated(&md, &book);

    win.emplace_back(px, qty);
    sum_pq += px * qty;
    sum_q  += qty;
    if (win.size() > W) {
      auto [opx, oq] = win.front(); win.pop_front();
      sum_pq -= opx * oq;
      sum_q  -= oq;
    }

    if (sum_q > 0.0) {
      const double expected = sum_pq / sum_q;
      EXPECT_NEAR(engine.get_vwap(), expected, 1e-9)
          << "i=" << i << " W=" << W << " sum_q=" << sum_q;
    } else {
      EXPECT_DOUBLE_EQ(engine.get_vwap(), 0.0);
    }
  }
}

TEST_F(FeatureEngineTest, OnTradeUpdate_RollingVWAP_MultiWraps)
{
  FeatureEngine engine(&logger);

  std::string symbol = "ETHUSDT";
  MarketOrderBook book(symbol, &logger);

  const size_t W = kVwapSize;
  const size_t N = 3 * W + 11;

  double sum_pq = 0.0, sum_q = 0.0;
  std::deque<std::pair<double,double>> win;

  for (size_t i = 0; i < N; ++i) {
    const double px  = 200.0 + 0.25 * static_cast<double>(i);
    const double qty = (i % 7 == 0) ? 10.0 : (1.0 + static_cast<double>(i % 3));

    MarketData md(common::MarketUpdateType::kTrade,
                  common::OrderId{42},
                  symbol,
                  common::Side::kTrade,
                  Price{px},
                  Qty{qty});

    engine.on_trade_updated(&md, &book);

    win.emplace_back(px, qty);
    sum_pq += px * qty;
    sum_q  += qty;
    if (win.size() > W) {
      auto [opx, oq] = win.front(); win.pop_front();
      sum_pq -= opx * oq;
      sum_q  -= oq;
    }
    
    if (i % (W / 3 + 1) == 0 || i + 1 == N) {
      ASSERT_GT(sum_q, 0.0);
      const double expected = sum_pq / sum_q;
      EXPECT_NEAR(engine.get_vwap(), expected, 1e-9)
          << "multi-wrap check at i=" << i;
    }
  }
}