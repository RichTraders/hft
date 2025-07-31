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
#include <gmock/gmock.h>
#include "feature_engine.h"

#include "logger.h"
#include "order_book.h"
#include "trade_engine.h"

using ::testing::HasSubstr;
using ::testing::_;
using namespace common;
using namespace trading;

class FeatureEngineTest : public ::testing::Test {
protected:
  void SetUp() override {
    market_pool = new MemoryPool<MarketData>(8);
    market_update_pool = new MemoryPool<MarketUpdateData>(8);

    TradeEngineCfg cfg;
    cfg.risk_cfg_.max_order_size_ = Qty{10};
    cfg.risk_cfg_.max_position_ = Qty{50};
    cfg.risk_cfg_.max_loss_ = -1000;

    ticker_cfg = new TradeEngineCfgHashMap{{"BTCUSDT", cfg}};

    trade_engine = new TradeEngine(&logger, market_update_pool, market_pool,
                                   *ticker_cfg);
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

  // BBO 세팅
  MarketOrderBook book("ETHUSDT", &logger);
  book.set_trade_engine(trade_engine);
  {
    const Price p = Price
        {100'000.};
    const Qty q{20.0};
    const MarketData md{MarketUpdateType::kAdd, OrderId{kOrderIdInvalid},
                        "ETHUSDT", Side::kBuy, p, q};
    book.on_market_data_updated(&md);
  }
  {
    const Price p = Price
        {200'000.};
    const Qty q{80.0};
    const MarketData md{MarketUpdateType::kAdd, OrderId{kOrderIdInvalid},
                        "ETHUSDT", Side::kSell, p, q};
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

  // BBO 세팅
  MarketOrderBook book("ETHUSDT", &logger);
  book.set_trade_engine(trade_engine);
  {
    const Price p = Price
        {100'000.};
    const Qty q{20.0};
    const MarketData md{MarketUpdateType::kAdd, OrderId{kOrderIdInvalid},
                        "ETHUSDT", Side::kBuy, p, q};
    book.on_market_data_updated(&md);
  }
  {
    const Price p = Price
        {200'000.};
    const Qty q{80.0};
    const MarketData md{MarketUpdateType::kAdd, OrderId{kOrderIdInvalid},
                        "ETHUSDT", Side::kSell, p, q};
    book.on_market_data_updated(&md);
  }

  const MarketData md{MarketUpdateType::kTrade, OrderId{kOrderIdInvalid},
                      "ETHUSDT", Side::kBuy, Price{200'000}, Qty{10.0}};
  book.on_market_data_updated(&md);

  double expected_ratio = md.qty.value / book.get_bbo()->ask_qty.value;

  engine.on_trade_updated(&md, &book);

  EXPECT_DOUBLE_EQ(engine.get_agg_trade_qty_ratio(), expected_ratio);
}