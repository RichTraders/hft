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
#include "gtest/gtest.h"
#include "trade_engine.h"
#include "order_book.h"

using namespace trading;
using namespace common;

class MarketOrderBookTest : public ::testing::Test {
protected:
  Logger logger_;
  //TradeEngine engine_;
  MarketOrderBook* book_;

public:
  // MarketOrderBookTest()
  //   : engine_(&logger_){}

protected:
  void SetUp() override {
    // trade_engine_ 주입
    book_ = new MarketOrderBook{"TEST_TICKER", &logger_};
    book_->set_trade_engine(nullptr);
  }

  void TearDown() override {
    if (book_)
      delete book_;
  }
};

// Clear
TEST_F(MarketOrderBookTest, ClearResetsOrderBookAndUpdatesBBO) {
  const MarketData md(MarketUpdateType::kClear, OrderId{kOrderIdInvalid},
                      "BTCUSDT",
                      Side::kBuy, Price{0.0}, Qty{0.0});

  book_->on_market_data_updated(&md);

  // BBO는 invalid 값이 되어야 한다
  const BBO* bbo = book_->get_bbo();
  EXPECT_EQ(bbo->bid_price, kPriceInvalid);
  EXPECT_EQ(bbo->ask_price, kPriceInvalid);
  EXPECT_EQ(bbo->bid_qty, kQtyInvalid);
  EXPECT_EQ(bbo->ask_qty, kQtyInvalid);
}

// Trade
TEST_F(MarketOrderBookTest, TradeInvokesTradeEngineAndSkipsOrderBookUpdate) {

  const Price price = Price{100000.00};
  const Qty qty = Qty{5.0};
  const MarketData md(MarketUpdateType::kAdd, OrderId{kOrderIdInvalid},
                      "BTCUSDT",
                      Side::kBuy, price, qty);

  book_->on_market_data_updated(&md);

  const Price trade_price = Price{100000.00};
  const Qty trade_qty = Qty{4.0};
  const MarketData trade_md = MarketData(MarketUpdateType::kTrade,
                                         OrderId{kOrderIdInvalid}, "BTCUSDT",
                                         Side::kBuy, trade_price, trade_qty);
  book_->on_market_data_updated(&trade_md);

  const BBO* bbo = book_->get_bbo();
  EXPECT_EQ(bbo->bid_price, price);
  EXPECT_EQ(bbo->ask_price, kPriceInvalid);
  EXPECT_EQ(bbo->bid_qty, qty.value-trade_qty.value);
  EXPECT_EQ(bbo->ask_qty, kQtyInvalid);
}

// Add
TEST_F(MarketOrderBookTest, AddOrder) {
  const Price price = Price{100000.00};
  const Qty qty = Qty{5.0};
  const MarketData md(MarketUpdateType::kAdd, OrderId{kOrderIdInvalid},
                      "BTCUSDT",
                      Side::kBuy, price, qty);

  book_->on_market_data_updated(&md);

  const BBO* bbo = book_->get_bbo();
  EXPECT_EQ(bbo->bid_price, price);
  EXPECT_EQ(bbo->ask_price, kPriceInvalid);
  EXPECT_EQ(bbo->bid_qty, qty);
  EXPECT_EQ(bbo->ask_qty, kQtyInvalid);
}

TEST_F(MarketOrderBookTest, AddOrders) {
  {
    const Price price = Price{100000.00};
    const Qty qty = Qty{5.0};
    const MarketData md(MarketUpdateType::kAdd, OrderId{kOrderIdInvalid},
                        "BTCUSDT",
                        Side::kBuy, price, qty);

    book_->on_market_data_updated(&md);

    const BBO* bbo = book_->get_bbo();
    EXPECT_EQ(bbo->bid_price, price);
    EXPECT_EQ(bbo->ask_price, kPriceInvalid);
    EXPECT_EQ(bbo->bid_qty, qty);
    EXPECT_EQ(bbo->ask_qty, kQtyInvalid);
  }
  {
    const Price price = Price{100001.00};
    const Qty qty = Qty{4.0};
    const MarketData md(MarketUpdateType::kAdd, OrderId{kOrderIdInvalid},
                        "BTCUSDT",
                        Side::kBuy, price, qty);

    book_->on_market_data_updated(&md);

    const BBO* bbo = book_->get_bbo();
    EXPECT_EQ(bbo->bid_price, price);
    EXPECT_EQ(bbo->ask_price, kPriceInvalid);
    EXPECT_EQ(bbo->bid_qty, qty);
    EXPECT_EQ(bbo->ask_qty, kQtyInvalid);
  }
  {
    const Price price = Price{100001.00};
    const Qty qty = Qty{3.0};
    const MarketData md(MarketUpdateType::kModify, OrderId{kOrderIdInvalid},
                        "BTCUSDT",
                        Side::kBuy, price, qty);

    book_->on_market_data_updated(&md);

    const BBO* bbo = book_->get_bbo();
    EXPECT_EQ(bbo->bid_price, price);
    EXPECT_EQ(bbo->ask_price, kPriceInvalid);
    EXPECT_EQ(bbo->bid_qty, qty);
    EXPECT_EQ(bbo->ask_qty, kQtyInvalid);
  }
  {
    const Price price = Price{100001.00};
    const Qty qty = Qty{2.0};
    const MarketData md(MarketUpdateType::kTrade, OrderId{kOrderIdInvalid},
                        "BTCUSDT",
                        Side::kBuy, price, qty);

    book_->on_market_data_updated(&md);

    const BBO* bbo = book_->get_bbo();
    EXPECT_EQ(bbo->bid_price, price);
    EXPECT_EQ(bbo->ask_price, kPriceInvalid);
    EXPECT_EQ(bbo->bid_qty, 1.0);
    EXPECT_EQ(bbo->ask_qty, kQtyInvalid);
  }
}

TEST_F(MarketOrderBookTest, AddBuyAndSellOrders) {
  {
    const Price price = Price{100000.00};
    const Qty qty = Qty{5.0};
    const MarketData md(MarketUpdateType::kAdd, OrderId{kOrderIdInvalid},
                        "BTCUSDT",
                        Side::kBuy, price, qty);

    book_->on_market_data_updated(&md);

    const BBO* bbo = book_->get_bbo();
    EXPECT_EQ(bbo->bid_price, price);
    EXPECT_EQ(bbo->ask_price, kPriceInvalid);
    EXPECT_EQ(bbo->bid_qty, qty);
    EXPECT_EQ(bbo->ask_qty, kQtyInvalid);
  }
  {
    const Price price = Price{100001.00};
    const Qty qty = Qty{4.0};
    const MarketData md(MarketUpdateType::kAdd, OrderId{kOrderIdInvalid},
                        "BTCUSDT",
                        Side::kBuy, price, qty);

    book_->on_market_data_updated(&md);

    const BBO* bbo = book_->get_bbo();
    EXPECT_EQ(bbo->bid_price, price);
    EXPECT_EQ(bbo->ask_price, kPriceInvalid);
    EXPECT_EQ(bbo->bid_qty, qty);
    EXPECT_EQ(bbo->ask_qty, kQtyInvalid);
  }
  {
    const Price price = Price{100001.00};
    const Qty qty = Qty{3.0};
    const MarketData md(MarketUpdateType::kModify, OrderId{kOrderIdInvalid},
                        "BTCUSDT",
                        Side::kBuy, price, qty);

    book_->on_market_data_updated(&md);

    const BBO* bbo = book_->get_bbo();
    EXPECT_EQ(bbo->bid_price, price);
    EXPECT_EQ(bbo->ask_price, kPriceInvalid);
    EXPECT_EQ(bbo->bid_qty, qty);
    EXPECT_EQ(bbo->ask_qty, kQtyInvalid);
  }
  {
    const Price price = Price{100000.50};
    const Qty qty = Qty{14.0};
    const MarketData md(MarketUpdateType::kAdd, OrderId{kOrderIdInvalid},
                        "BTCUSDT",
                        Side::kBuy, price, qty);

    book_->on_market_data_updated(&md);

    const BBO* bbo = book_->get_bbo();
    EXPECT_EQ(bbo->bid_price, 100001.0);
    EXPECT_EQ(bbo->ask_price, kPriceInvalid);
    EXPECT_EQ(bbo->bid_qty, 3.0);
    EXPECT_EQ(bbo->ask_qty, kQtyInvalid);
  }
  {
    const Price price = Price{100000.00};
    const Qty qty = Qty{2.0};
    const MarketData md(MarketUpdateType::kAdd, OrderId{kOrderIdInvalid},
                        "BTCUSDT",
                        Side::kSell, price, qty);

    book_->on_market_data_updated(&md);

    const BBO* bbo = book_->get_bbo();
    EXPECT_EQ(bbo->bid_price, 100001.00);
    EXPECT_EQ(bbo->ask_price, price);
    EXPECT_EQ(bbo->bid_qty, 3.0);
    EXPECT_EQ(bbo->ask_qty, qty);
  }
  {
    const Price price = Price{99999.00};
    const Qty qty = Qty{3.0};
    const MarketData md(MarketUpdateType::kAdd, OrderId{kOrderIdInvalid},
                        "BTCUSDT",
                        Side::kSell, price, qty);

    book_->on_market_data_updated(&md);

    const BBO* bbo = book_->get_bbo();
    EXPECT_EQ(bbo->bid_price, 100001.00);
    EXPECT_EQ(bbo->ask_price, price);
    EXPECT_EQ(bbo->bid_qty, 3.0);
    EXPECT_EQ(bbo->ask_qty, qty);
  }
  {
    const Price price = Price{100001.00};
    const Qty qty = Qty{3.0};
    const MarketData md(MarketUpdateType::kModify, OrderId{kOrderIdInvalid},
                        "BTCUSDT",
                        Side::kSell, price, qty);

    book_->on_market_data_updated(&md);

    const BBO* bbo = book_->get_bbo();
    EXPECT_EQ(bbo->bid_price, 100001.00);
    EXPECT_EQ(bbo->ask_price, 99999.00); // previous
    EXPECT_EQ(bbo->bid_qty, 3.0);
    EXPECT_EQ(bbo->ask_qty, qty);
  }
}

// Delete
TEST_F(MarketOrderBookTest, DeleteOrder) {
  {
    const Price price = Price{100000.00};
    const Qty qty = Qty{5.0};
    const MarketData md(MarketUpdateType::kAdd, OrderId{kOrderIdInvalid},
                        "BTCUSDT",
                        Side::kSell, price, qty);
    book_->on_market_data_updated(&md);
    const BBO* bbo = book_->get_bbo();
    EXPECT_EQ(bbo->bid_price, kPriceInvalid);
    EXPECT_EQ(bbo->ask_price, price);
    EXPECT_EQ(bbo->bid_qty, kQtyInvalid);
    EXPECT_EQ(bbo->ask_qty, qty);
  }
  {
    const Price cancel_price = Price{100000.00};
    const Qty cancel_qty = Qty{kQtyInvalid};
    const MarketData cancel_md(MarketUpdateType::kCancel,
                               OrderId{kOrderIdInvalid},
                               "BTCUSDT",
                               Side::kSell, cancel_price, cancel_qty);

    book_->on_market_data_updated(&cancel_md);
    const BBO* bbo = book_->get_bbo();
    EXPECT_EQ(bbo->bid_price, kPriceInvalid);
    EXPECT_EQ(bbo->ask_price, kPriceInvalid);
    EXPECT_EQ(bbo->bid_qty, kQtyInvalid);
    EXPECT_EQ(bbo->ask_qty, kQtyInvalid);
  }
}