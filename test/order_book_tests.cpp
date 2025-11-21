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
#include "../hft/core/NewOroFix44/response_manager.h"
#include "gtest/gtest.h"
#include "ini_config.hpp"
#include "order_book.h"
#include "strategy_config.hpp"
#include "trade_engine.h"

using namespace trading;
using namespace common;

using TestTradeEngine = trading::TradeEngine<SelectedStrategy>;
using TestOrderBook = trading::MarketOrderBook<SelectedStrategy>;

class MarketOrderBookTest : public ::testing::Test {
public:
  static std::unique_ptr<Logger> logger;
 protected:

  TestOrderBook* book_;
  ResponseManager* response_manager_;
  TestTradeEngine* trade_engine_;

  static void SetUpTestSuite() {
    logger = std::make_unique<Logger>();
    INI_CONFIG.load("resources/config.ini");
  }

  void SetUp() override {
    TradeEngineCfgHashMap temp;
    RiskCfg risk = {.max_order_size_ = Qty{1000.},
                    .max_position_ = Qty{1000.},
                    .max_loss_ = 1000.};
    TradeEngineCfg tempcfg = {
        .clip_ = Qty{100000}, .threshold_ = 10, .risk_cfg_ = risk};
    temp.emplace(INI_CONFIG.get("meta", "ticker"), tempcfg);
    auto pool = std::make_unique<MemoryPool<MarketUpdateData>>(4096);
    auto pool2 = std::make_unique<MemoryPool<MarketData>>(4096);

    auto execution_report_pool =
        std::make_unique<MemoryPool<ExecutionReport>>(1024);
    auto order_cancel_reject_pool =
        std::make_unique<MemoryPool<OrderCancelReject>>(1024);
    auto order_mass_cancel_report_pool =
        std::make_unique<MemoryPool<OrderMassCancelReport>>(1024);
    response_manager_ = new ResponseManager(
        logger.get(), execution_report_pool.get(), order_cancel_reject_pool.get(),
        order_mass_cancel_report_pool.get());

    trade_engine_ = new TestTradeEngine(logger.get(), pool.get(),
                                        pool2.get(), response_manager_, temp);
    // trade_engine_ 주입
    book_ = new TestOrderBook{INI_CONFIG.get("meta", "ticker"), logger.get()};
    book_->set_trade_engine(trade_engine_);
  }

  void TearDown() override {
    if (response_manager_)
      delete response_manager_;

    if (trade_engine_)
      delete trade_engine_;

    if (book_)
      delete book_;
  }
};
std::unique_ptr<Logger> MarketOrderBookTest::logger;

TEST_F(MarketOrderBookTest, ClearResetsOrderBookAndUpdatesBBO) {
  TickerId symbol = INI_CONFIG.get("meta", "ticker");
  const MarketData md(MarketUpdateType::kClear, OrderId{kOrderIdInvalid},
                      symbol, Side::kBuy, Price{0.0}, Qty{0.0});

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
  TickerId symbol = INI_CONFIG.get("meta", "ticker");
  const Price price = Price{100000.00};
  const Qty qty = Qty{5.0};
  const MarketData md(MarketUpdateType::kAdd, OrderId{kOrderIdInvalid}, symbol,
                      Side::kBuy, price, qty);

  book_->on_market_data_updated(&md);

  const Price trade_price = Price{100000.00};
  const Qty trade_qty = Qty{4.0};
  const MarketData trade_md =
      MarketData(MarketUpdateType::kTrade, OrderId{kOrderIdInvalid}, symbol,
                 Side::kBuy, trade_price, trade_qty);
  book_->on_market_data_updated(&trade_md);

  const BBO* bbo = book_->get_bbo();
  EXPECT_EQ(bbo->bid_price, price);
  EXPECT_EQ(bbo->ask_price, kPriceInvalid);
  EXPECT_EQ(bbo->bid_qty, qty.value - trade_qty.value);
  EXPECT_EQ(bbo->ask_qty, kQtyInvalid);
}

TEST_F(MarketOrderBookTest, AddOrder) {
  TickerId symbol = INI_CONFIG.get("meta", "ticker");
  const Price price = Price{100000.00};
  const Qty qty = Qty{5.0};
  const MarketData md(MarketUpdateType::kAdd, OrderId{kOrderIdInvalid}, symbol,
                      Side::kBuy, price, qty);

  book_->on_market_data_updated(&md);

  const BBO* bbo = book_->get_bbo();
  EXPECT_EQ(bbo->bid_price, price);
  EXPECT_EQ(bbo->ask_price, kPriceInvalid);
  EXPECT_EQ(bbo->bid_qty, qty);
  EXPECT_EQ(bbo->ask_qty, kQtyInvalid);
}

TEST_F(MarketOrderBookTest, AddOrders) {
  TickerId symbol = INI_CONFIG.get("meta", "ticker");
  {
    const Price price = Price{100000.00};
    const Qty qty = Qty{5.0};
    const MarketData md(MarketUpdateType::kAdd, OrderId{kOrderIdInvalid},
                        symbol, Side::kBuy, price, qty);

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
                        symbol, Side::kBuy, price, qty);

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
                        symbol, Side::kBuy, price, qty);

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
                        symbol, Side::kBuy, price, qty);

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
    TickerId symbol = INI_CONFIG.get("meta", "ticker");
    const Price price = Price{100000.00};
    const Qty qty = Qty{5.0};
    const MarketData md(MarketUpdateType::kAdd, OrderId{kOrderIdInvalid},
                        symbol, Side::kBuy, price, qty);

    book_->on_market_data_updated(&md);

    const BBO* bbo = book_->get_bbo();
    EXPECT_EQ(bbo->bid_price, price);
    EXPECT_EQ(bbo->ask_price, kPriceInvalid);
    EXPECT_EQ(bbo->bid_qty, qty);
    EXPECT_EQ(bbo->ask_qty, kQtyInvalid);
  }
  {
    TickerId symbol = INI_CONFIG.get("meta", "ticker");
    const Price price = Price{100001.00};
    const Qty qty = Qty{4.0};
    const MarketData md(MarketUpdateType::kAdd, OrderId{kOrderIdInvalid},
                        symbol, Side::kBuy, price, qty);

    book_->on_market_data_updated(&md);

    const BBO* bbo = book_->get_bbo();
    EXPECT_EQ(bbo->bid_price, price);
    EXPECT_EQ(bbo->ask_price, kPriceInvalid);
    EXPECT_EQ(bbo->bid_qty, qty);
    EXPECT_EQ(bbo->ask_qty, kQtyInvalid);
  }
  {
    TickerId symbol = INI_CONFIG.get("meta", "ticker");
    const Price price = Price{100001.00};
    const Qty qty = Qty{3.0};
    const MarketData md(MarketUpdateType::kModify, OrderId{kOrderIdInvalid},
                        symbol, Side::kBuy, price, qty);

    book_->on_market_data_updated(&md);

    const BBO* bbo = book_->get_bbo();
    EXPECT_EQ(bbo->bid_price, price);
    EXPECT_EQ(bbo->ask_price, kPriceInvalid);
    EXPECT_EQ(bbo->bid_qty, qty);
    EXPECT_EQ(bbo->ask_qty, kQtyInvalid);
  }
  {
    TickerId symbol = INI_CONFIG.get("meta", "ticker");
    const Price price = Price{100000.50};
    const Qty qty = Qty{14.0};
    const MarketData md(MarketUpdateType::kAdd, OrderId{kOrderIdInvalid},
                        symbol, Side::kBuy, price, qty);

    book_->on_market_data_updated(&md);

    const BBO* bbo = book_->get_bbo();
    EXPECT_EQ(bbo->bid_price, 100001.0);
    EXPECT_EQ(bbo->ask_price, kPriceInvalid);
    EXPECT_EQ(bbo->bid_qty, 3.0);
    EXPECT_EQ(bbo->ask_qty, kQtyInvalid);
  }
  {
    TickerId symbol = INI_CONFIG.get("meta", "ticker");
    const Price price = Price{100000.00};
    const Qty qty = Qty{2.0};
    const MarketData md(MarketUpdateType::kAdd, OrderId{kOrderIdInvalid},
                        symbol, Side::kSell, price, qty);

    book_->on_market_data_updated(&md);

    const BBO* bbo = book_->get_bbo();
    EXPECT_EQ(bbo->bid_price, 100001.00);
    EXPECT_EQ(bbo->ask_price, price);
    EXPECT_EQ(bbo->bid_qty, 3.0);
    EXPECT_EQ(bbo->ask_qty, qty);
  }
  {
    TickerId symbol = INI_CONFIG.get("meta", "ticker");
    const Price price = Price{99999.00};
    const Qty qty = Qty{3.0};
    const MarketData md(MarketUpdateType::kAdd, OrderId{kOrderIdInvalid},
                        symbol, Side::kSell, price, qty);

    book_->on_market_data_updated(&md);

    const BBO* bbo = book_->get_bbo();
    EXPECT_EQ(bbo->bid_price, 100001.00);
    EXPECT_EQ(bbo->ask_price, price);
    EXPECT_EQ(bbo->bid_qty, 3.0);
    EXPECT_EQ(bbo->ask_qty, qty);
  }
  {
    TickerId symbol = INI_CONFIG.get("meta", "ticker");
    const Price price = Price{100001.00};
    const Qty qty = Qty{3.0};
    const MarketData md(MarketUpdateType::kModify, OrderId{kOrderIdInvalid},
                        symbol, Side::kSell, price, qty);

    book_->on_market_data_updated(&md);

    const BBO* bbo = book_->get_bbo();
    EXPECT_EQ(bbo->bid_price, 100001.00);
    EXPECT_EQ(bbo->ask_price, 99999.00);  // previous
    EXPECT_EQ(bbo->bid_qty, 3.0);
    EXPECT_EQ(bbo->ask_qty, qty);
  }
}

// Delete
TEST_F(MarketOrderBookTest, DeleteOrder) {
  {
    TickerId symbol = INI_CONFIG.get("meta", "ticker");
    const Price price = Price{100000.00};
    const Qty qty = Qty{5.0};
    const MarketData md(MarketUpdateType::kAdd, OrderId{kOrderIdInvalid},
                        symbol, Side::kSell, price, qty);
    book_->on_market_data_updated(&md);
    const BBO* bbo = book_->get_bbo();
    EXPECT_EQ(bbo->bid_price, kPriceInvalid);
    EXPECT_EQ(bbo->ask_price, price);
    EXPECT_EQ(bbo->bid_qty, kQtyInvalid);
    EXPECT_EQ(bbo->ask_qty, qty);
  }
  {
    TickerId symbol = INI_CONFIG.get("meta", "ticker");
    const Price cancel_price = Price{100000.00};
    const Qty cancel_qty = Qty{kQtyInvalid};
    const MarketData cancel_md(MarketUpdateType::kCancel,
                               OrderId{kOrderIdInvalid}, symbol, Side::kSell,
                               cancel_price, cancel_qty);

    book_->on_market_data_updated(&cancel_md);
    const BBO* bbo = book_->get_bbo();
    EXPECT_EQ(bbo->bid_price, kPriceInvalid);
    EXPECT_EQ(bbo->ask_price, kPriceInvalid);
    EXPECT_EQ(bbo->bid_qty, kQtyInvalid);
    EXPECT_EQ(bbo->ask_qty, kQtyInvalid);
  }
}

TEST_F(MarketOrderBookTest, FindInBucket) {
  TickerId symbol = INI_CONFIG.get("meta", "ticker");
  trading::Bucket b;
  // 모두 비활성
  for (auto& w : b.bitmap)
    w = 0;
  EXPECT_EQ(book_->find_in_bucket(&b, true), -1);
  EXPECT_EQ(book_->find_in_bucket(&b, false), -1);

  // 워드0 비트2, 워드1 비트5 활성화
  b.bitmap[0] = (1ULL << 2);
  b.bitmap[1] = (1ULL << 5);

  EXPECT_EQ(book_->find_in_bucket(&b, false), 2);      // lowest → offset 2
  EXPECT_EQ(book_->find_in_bucket(&b, true), 64 + 5);  // highest → offset 69
}

TEST_F(MarketOrderBookTest, NextActiveIdx) {
  for (int idx : {10, 20, 30}) {
    TickerId symbol = INI_CONFIG.get("meta", "ticker");
    Price p =
        Price{static_cast<double>(kMinPriceInt + idx) / kTickMultiplierInt};
    Qty q{1.0};
    MarketData md{MarketUpdateType::kAdd, OrderId{0}, symbol, Side::kBuy, p, q};
    book_->on_market_data_updated(&md);
  }
  const BBO* bbo = book_->get_bbo();
  EXPECT_EQ(bbo->bid_price, indexToPrice(30));

  EXPECT_EQ(book_->next_active_idx(true, 30), 20);
  EXPECT_EQ(book_->next_active_idx(true, 20), 10);
  EXPECT_EQ(book_->next_active_idx(true, 10), -1);

  for (int idx : {100, 110, 120}) {
    TickerId symbol = INI_CONFIG.get("meta", "ticker");
    Price p =
        Price{static_cast<double>(kMinPriceInt + idx) / kTickMultiplierInt};
    Qty q{2.0};
    MarketData md{
        MarketUpdateType::kAdd, OrderId{1}, symbol, Side::kSell, p, q};
    book_->on_market_data_updated(&md);
  }
  EXPECT_EQ(bbo->ask_price, indexToPrice(100));
  EXPECT_EQ(book_->next_active_idx(false, 100), 110);
  EXPECT_EQ(book_->next_active_idx(false, 110), 120);
  EXPECT_EQ(book_->next_active_idx(false, 120), -1);
}

TEST_F(MarketOrderBookTest, NextActiveIdxWithCancel) {
  for (int idx : {10, 20, 30}) {
    TickerId symbol = INI_CONFIG.get("meta", "ticker");
    Price p =
        Price{static_cast<double>(kMinPriceInt + idx) / kTickMultiplierInt};
    Qty q{1.0};
    MarketData md{MarketUpdateType::kAdd, OrderId{0}, symbol, Side::kBuy, p, q};
    book_->on_market_data_updated(&md);
  }
  const BBO* bbo = book_->get_bbo();
  EXPECT_EQ(bbo->bid_price, indexToPrice(30));

  EXPECT_EQ(book_->next_active_idx(true, 30), 20);
  EXPECT_EQ(book_->next_active_idx(true, 20), 10);
  EXPECT_EQ(book_->next_active_idx(true, 10), -1);

  for (int idx : {20}) {
    TickerId symbol = INI_CONFIG.get("meta", "ticker");
    Price p =
        Price{static_cast<double>(kMinPriceInt + idx) / kTickMultiplierInt};
    Qty q{1.0};
    MarketData md{
        MarketUpdateType::kCancel, OrderId{0}, symbol, Side::kBuy, p, q};
    book_->on_market_data_updated(&md);
  }
  EXPECT_EQ(bbo->bid_price, indexToPrice(30));
  EXPECT_EQ(book_->next_active_idx(true, 30), 10);

  for (int idx : {100, 110, 120}) {
    TickerId symbol = INI_CONFIG.get("meta", "ticker");
    Price p =
        Price{static_cast<double>(kMinPriceInt + idx) / kTickMultiplierInt};
    Qty q{2.0};
    MarketData md{
        MarketUpdateType::kAdd, OrderId{1}, symbol, Side::kSell, p, q};
    book_->on_market_data_updated(&md);
  }
  EXPECT_EQ(bbo->ask_price, indexToPrice(100));
  EXPECT_EQ(book_->next_active_idx(false, 100), 110);
  EXPECT_EQ(book_->next_active_idx(false, 110), 120);
  EXPECT_EQ(book_->next_active_idx(false, 120), -1);
}

TEST_F(MarketOrderBookTest, PeekLevels) {
  for (int idx : {5, 15, 25, 35, 45}) {
    TickerId symbol = INI_CONFIG.get("meta", "ticker");
    Price p =
        Price{static_cast<double>(kMinPriceInt + idx) / kTickMultiplierInt};
    Qty q{1.0};
    MarketData md{MarketUpdateType::kAdd, OrderId{2}, symbol, Side::kBuy, p, q};
    book_->on_market_data_updated(&md);
  }
  const BBO* bbo = book_->get_bbo();
  EXPECT_EQ(bbo->bid_price, indexToPrice(45));

  auto bids = book_->peek_levels(true, 3);
  std::vector want_bids = {35, 25, 15};
  EXPECT_EQ(bids, want_bids);

  for (int idx : {200, 210, 220}) {
    TickerId symbol = INI_CONFIG.get("meta", "ticker");
    Price p =
        Price{static_cast<double>(kMinPriceInt + idx) / kTickMultiplierInt};
    Qty q{3.0};
    MarketData md{
        MarketUpdateType::kAdd, OrderId{3}, symbol, Side::kSell, p, q};
    book_->on_market_data_updated(&md);
  }
  EXPECT_EQ(bbo->ask_price, indexToPrice(200));

  auto asks = book_->peek_levels(false, 2);
  std::vector want_asks = {210, 220};
  EXPECT_EQ(asks, want_asks);
}
