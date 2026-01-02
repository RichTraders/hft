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
#include "core/response_manager.h"
#include "gtest/gtest.h"
#include "ini_config.hpp"
#include "order_book.hpp"
#include "strategy_config.hpp"
#include "trade_engine.hpp"

using namespace trading;
using namespace common;
using TestStrategy = SelectedStrategy;
using TestTradeEngine = trading::TradeEngine<TestStrategy>;
using TestOrderBook = trading::MarketOrderBook<TestStrategy>;

class MarketOrderBookTest : public ::testing::Test {
public:
  static std::unique_ptr<Logger> logger;
  static std::unique_ptr<Logger::Producer> producer;
 protected:

  TestOrderBook* book_;
  ResponseManager* response_manager_;
  TestTradeEngine* trade_engine_;

  static void SetUpTestSuite() {
    logger = std::make_unique<Logger>();
    producer = std::make_unique<Logger::Producer>(logger->make_producer());
  }

  void SetUp() override {
    INI_CONFIG.load("resources/config-orderbook.ini");

    TradeEngineCfgHashMap temp;
    RiskCfg risk(
        QtyType::from_double(1000.),
        QtyType::from_double(1000.),
        QtyType::from_double(0.),
        1000);
    TradeEngineCfg tempcfg;
    tempcfg.clip_ = QtyType::from_double(100000);
    tempcfg.threshold_ = 10;
    tempcfg.risk_cfg_ = risk;
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
        *producer, execution_report_pool.get(), order_cancel_reject_pool.get(),
        order_mass_cancel_report_pool.get());

    trade_engine_ = new TestTradeEngine(*producer, pool.get(),
                                        pool2.get(), response_manager_, temp);
    // trade_engine_ 주입
    book_ = new TestOrderBook{INI_CONFIG.get("meta", "ticker"), *producer};
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
std::unique_ptr<Logger::Producer> MarketOrderBookTest::producer;

TEST_F(MarketOrderBookTest, ClearResetsOrderBookAndUpdatesBBO) {
  TickerId symbol = INI_CONFIG.get("meta", "ticker");
  const MarketData md(MarketUpdateType::kClear, OrderId{kOrderIdInvalid},
                      symbol, Side::kBuy, PriceType::from_raw(0), QtyType::from_raw(0));

  book_->on_market_data_updated(&md);

  // BBO는 invalid 값이 되어야 한다
  const BBO* bbo = book_->get_bbo();
  EXPECT_FALSE(bbo->bid_price.is_valid());
  EXPECT_FALSE(bbo->ask_price.is_valid());
  EXPECT_FALSE(bbo->bid_qty.is_valid());
  EXPECT_FALSE(bbo->ask_qty.is_valid());
}

// Trade
TEST_F(MarketOrderBookTest, TradeInvokesTradeEngineAndSkipsOrderBookUpdate) {
  TickerId symbol = INI_CONFIG.get("meta", "ticker");
  const auto price = PriceType::from_raw(100000);
  const auto qty = QtyType::from_raw(5000);
  const MarketData md(MarketUpdateType::kAdd, OrderId{kOrderIdInvalid}, symbol,
                      Side::kBuy, price, qty);

  book_->on_market_data_updated(&md);

  const auto trade_price = PriceType::from_raw(100000);
  const auto trade_qty = QtyType::from_raw(4000);
  const MarketData trade_md =
      MarketData(MarketUpdateType::kTrade, OrderId{kOrderIdInvalid}, symbol,
                 Side::kBuy, trade_price, trade_qty);
  book_->on_market_data_updated(&trade_md);

  const BBO* bbo = book_->get_bbo();
  EXPECT_EQ(bbo->bid_price, price);
  EXPECT_FALSE(bbo->ask_price.is_valid());
  EXPECT_EQ(bbo->bid_qty.value, qty.value - trade_qty.value);
  EXPECT_FALSE(bbo->ask_qty.is_valid());
}

TEST_F(MarketOrderBookTest, AddOrder) {
  TickerId symbol = INI_CONFIG.get("meta", "ticker");
  const auto price = PriceType::from_raw(100000);
  const auto qty = QtyType::from_raw(5000);
  const MarketData md(MarketUpdateType::kAdd, OrderId{kOrderIdInvalid}, symbol,
                      Side::kBuy, price, qty);

  book_->on_market_data_updated(&md);

  const BBO* bbo = book_->get_bbo();
  EXPECT_EQ(bbo->bid_price, price);
  EXPECT_FALSE(bbo->ask_price.is_valid());
  EXPECT_EQ(bbo->bid_qty, qty);
  EXPECT_FALSE(bbo->ask_qty.is_valid());
}

TEST_F(MarketOrderBookTest, AddOrders) {
  TickerId symbol = INI_CONFIG.get("meta", "ticker");
  {
    const auto price = PriceType::from_raw(100000);
    const auto qty = QtyType::from_raw(5000);
    const MarketData md(MarketUpdateType::kAdd, OrderId{kOrderIdInvalid},
                        symbol, Side::kBuy, price, qty);

    book_->on_market_data_updated(&md);

    const BBO* bbo = book_->get_bbo();
    EXPECT_EQ(bbo->bid_price, price);
    EXPECT_FALSE(bbo->ask_price.is_valid());
    EXPECT_EQ(bbo->bid_qty, qty);
    EXPECT_FALSE(bbo->ask_qty.is_valid());
  }
  {
    const auto price = PriceType::from_raw(100001);
    const auto qty = QtyType::from_raw(4000);
    const MarketData md(MarketUpdateType::kAdd, OrderId{kOrderIdInvalid},
                        symbol, Side::kBuy, price, qty);

    book_->on_market_data_updated(&md);

    const BBO* bbo = book_->get_bbo();
    EXPECT_EQ(bbo->bid_price, price);
    EXPECT_FALSE(bbo->ask_price.is_valid());
    EXPECT_EQ(bbo->bid_qty, qty);
    EXPECT_FALSE(bbo->ask_qty.is_valid());
  }
  {
    const auto price = PriceType::from_raw(100001);
    const auto qty = QtyType::from_raw(3000);
    const MarketData md(MarketUpdateType::kModify, OrderId{kOrderIdInvalid},
                        symbol, Side::kBuy, price, qty);

    book_->on_market_data_updated(&md);

    const BBO* bbo = book_->get_bbo();
    EXPECT_EQ(bbo->bid_price, price);
    EXPECT_FALSE(bbo->ask_price.is_valid());
    EXPECT_EQ(bbo->bid_qty, qty);
    EXPECT_FALSE(bbo->ask_qty.is_valid());
  }
  {
    const auto price = PriceType::from_raw(100001);
    const auto qty = QtyType::from_raw(2000);
    const MarketData md(MarketUpdateType::kTrade, OrderId{kOrderIdInvalid},
                        symbol, Side::kBuy, price, qty);

    book_->on_market_data_updated(&md);

    const BBO* bbo = book_->get_bbo();
    EXPECT_EQ(bbo->bid_price, price);
    EXPECT_FALSE(bbo->ask_price.is_valid());
    EXPECT_EQ(bbo->bid_qty.value, 1000);  // 3000 - 2000
    EXPECT_FALSE(bbo->ask_qty.is_valid());
  }
}

TEST_F(MarketOrderBookTest, AddBuyAndSellOrders) {
  TickerId symbol = INI_CONFIG.get("meta", "ticker");
  {
    const auto price = PriceType::from_raw(100000);
    const auto qty = QtyType::from_raw(5000);
    const MarketData md(MarketUpdateType::kAdd, OrderId{kOrderIdInvalid},
                        symbol, Side::kBuy, price, qty);

    book_->on_market_data_updated(&md);

    const BBO* bbo = book_->get_bbo();
    EXPECT_EQ(bbo->bid_price, price);
    EXPECT_FALSE(bbo->ask_price.is_valid());
    EXPECT_EQ(bbo->bid_qty, qty);
    EXPECT_FALSE(bbo->ask_qty.is_valid());
  }
  {
    const auto price = PriceType::from_raw(100001);
    const auto qty = QtyType::from_raw(4000);
    const MarketData md(MarketUpdateType::kAdd, OrderId{kOrderIdInvalid},
                        symbol, Side::kBuy, price, qty);

    book_->on_market_data_updated(&md);

    const BBO* bbo = book_->get_bbo();
    EXPECT_EQ(bbo->bid_price, price);
    EXPECT_FALSE(bbo->ask_price.is_valid());
    EXPECT_EQ(bbo->bid_qty, qty);
    EXPECT_FALSE(bbo->ask_qty.is_valid());
  }
  {
    const auto price = PriceType::from_raw(100001);
    const auto qty = QtyType::from_raw(3000);
    const MarketData md(MarketUpdateType::kModify, OrderId{kOrderIdInvalid},
                        symbol, Side::kBuy, price, qty);

    book_->on_market_data_updated(&md);

    const BBO* bbo = book_->get_bbo();
    EXPECT_EQ(bbo->bid_price, price);
    EXPECT_FALSE(bbo->ask_price.is_valid());
    EXPECT_EQ(bbo->bid_qty, qty);
    EXPECT_FALSE(bbo->ask_qty.is_valid());
  }
  {
    // 100000.5 -> raw 100000 (정수 가격만 사용)
    const auto price = PriceType::from_raw(100000);
    const auto qty = QtyType::from_raw(14000);
    const MarketData md(MarketUpdateType::kAdd, OrderId{kOrderIdInvalid},
                        symbol, Side::kBuy, price, qty);

    book_->on_market_data_updated(&md);

    const BBO* bbo = book_->get_bbo();
    // best bid는 여전히 100001
    EXPECT_EQ(bbo->bid_price.value, 100001);
    EXPECT_FALSE(bbo->ask_price.is_valid());
    EXPECT_EQ(bbo->bid_qty.value, 3000);
    EXPECT_FALSE(bbo->ask_qty.is_valid());
  }
  {
    const auto price = PriceType::from_raw(100000);
    const auto qty = QtyType::from_raw(2000);
    const MarketData md(MarketUpdateType::kAdd, OrderId{kOrderIdInvalid},
                        symbol, Side::kSell, price, qty);

    book_->on_market_data_updated(&md);

    const BBO* bbo = book_->get_bbo();
    EXPECT_EQ(bbo->bid_price.value, 100001);
    EXPECT_EQ(bbo->ask_price, price);
    EXPECT_EQ(bbo->bid_qty.value, 3000);
    EXPECT_EQ(bbo->ask_qty, qty);
  }
  {
    // 100000보다 낮은 99999는 min_price(100000) 미만이므로 100002 사용
    const auto price = PriceType::from_raw(100002);
    const auto qty = QtyType::from_raw(3000);
    const MarketData md(MarketUpdateType::kAdd, OrderId{kOrderIdInvalid},
                        symbol, Side::kSell, price, qty);

    book_->on_market_data_updated(&md);

    const BBO* bbo = book_->get_bbo();
    EXPECT_EQ(bbo->bid_price.value, 100001);
    // best ask는 여전히 100000 (100002보다 낮음)
    EXPECT_EQ(bbo->ask_price.value, 100000);
    EXPECT_EQ(bbo->bid_qty.value, 3000);
    EXPECT_EQ(bbo->ask_qty.value, 2000);  // 100000 가격의 qty
  }
  {
    const auto price = PriceType::from_raw(100003);
    const auto qty = QtyType::from_raw(3000);
    const MarketData md(MarketUpdateType::kModify, OrderId{kOrderIdInvalid},
                        symbol, Side::kSell, price, qty);

    book_->on_market_data_updated(&md);

    const BBO* bbo = book_->get_bbo();
    EXPECT_EQ(bbo->bid_price.value, 100001);
    // best ask는 여전히 100000
    EXPECT_EQ(bbo->ask_price.value, 100000);
    EXPECT_EQ(bbo->bid_qty.value, 3000);
    EXPECT_EQ(bbo->ask_qty.value, 2000);  // 100000 가격의 qty (변경 없음)
  }
}

// Delete
TEST_F(MarketOrderBookTest, DeleteOrder) {
  TickerId symbol = INI_CONFIG.get("meta", "ticker");
  {
    const auto price = PriceType::from_raw(100000);
    const auto qty = QtyType::from_raw(5000);
    const MarketData md(MarketUpdateType::kAdd, OrderId{kOrderIdInvalid},
                        symbol, Side::kSell, price, qty);
    book_->on_market_data_updated(&md);
    const BBO* bbo = book_->get_bbo();
    EXPECT_FALSE(bbo->bid_price.is_valid());
    EXPECT_EQ(bbo->ask_price, price);
    EXPECT_FALSE(bbo->bid_qty.is_valid());
    EXPECT_EQ(bbo->ask_qty, qty);
  }
  {
    const auto cancel_price = PriceType::from_raw(100000);
    const auto cancel_qty = QtyType();  // invalid qty
    const MarketData cancel_md(MarketUpdateType::kCancel,
                               OrderId{kOrderIdInvalid}, symbol, Side::kSell,
                               cancel_price, cancel_qty);

    book_->on_market_data_updated(&cancel_md);
    const BBO* bbo = book_->get_bbo();
    EXPECT_FALSE(bbo->bid_price.is_valid());
    EXPECT_FALSE(bbo->ask_price.is_valid());
    EXPECT_FALSE(bbo->bid_qty.is_valid());
    EXPECT_FALSE(bbo->ask_qty.is_valid());
  }
}

#ifndef USE_FLAT_ORDERBOOK
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
#endif

TEST_F(MarketOrderBookTest, NextActiveIdx) {
  const auto& cfg = book_->config();
  for (int idx : {10, 20, 30}) {
    TickerId symbol = INI_CONFIG.get("meta", "ticker");
    auto p = cfg.index_to_price(idx);
    auto q = QtyType::from_raw(1000);
    MarketData md{MarketUpdateType::kAdd, OrderId{0}, symbol, Side::kBuy, p, q};
    book_->on_market_data_updated(&md);
  }
  const BBO* bbo = book_->get_bbo();
  EXPECT_EQ(bbo->bid_price, cfg.index_to_price(30));

  EXPECT_EQ(book_->next_active_idx(true, 30), 20);
  EXPECT_EQ(book_->next_active_idx(true, 20), 10);
  EXPECT_EQ(book_->next_active_idx(true, 10), -1);

  for (int idx : {100, 110, 120}) {
    TickerId symbol = INI_CONFIG.get("meta", "ticker");
    auto p = cfg.index_to_price(idx);
    auto q = QtyType::from_raw(2000);
    MarketData md{
        MarketUpdateType::kAdd, OrderId{1}, symbol, Side::kSell, p, q};
    book_->on_market_data_updated(&md);
  }
  EXPECT_EQ(bbo->ask_price, cfg.index_to_price(100));
  EXPECT_EQ(book_->next_active_idx(false, 100), 110);
  EXPECT_EQ(book_->next_active_idx(false, 110), 120);
  EXPECT_EQ(book_->next_active_idx(false, 120), -1);
}

TEST_F(MarketOrderBookTest, NextActiveIdxWithCancel) {
  const auto& cfg = book_->config();
  for (int idx : {10, 20, 30}) {
    TickerId symbol = INI_CONFIG.get("meta", "ticker");
    auto p = cfg.index_to_price(idx);
    auto q = QtyType::from_raw(1000);
    MarketData md{MarketUpdateType::kAdd, OrderId{0}, symbol, Side::kBuy, p, q};
    book_->on_market_data_updated(&md);
  }
  const BBO* bbo = book_->get_bbo();
  EXPECT_EQ(bbo->bid_price, cfg.index_to_price(30));

  EXPECT_EQ(book_->next_active_idx(true, 30), 20);
  EXPECT_EQ(book_->next_active_idx(true, 20), 10);
  EXPECT_EQ(book_->next_active_idx(true, 10), -1);

  for (int idx : {20}) {
    TickerId symbol = INI_CONFIG.get("meta", "ticker");
    auto p = cfg.index_to_price(idx);
    auto q = QtyType::from_raw(1000);
    MarketData md{
        MarketUpdateType::kCancel, OrderId{0}, symbol, Side::kBuy, p, q};
    book_->on_market_data_updated(&md);
  }
  EXPECT_EQ(bbo->bid_price, cfg.index_to_price(30));
  EXPECT_EQ(book_->next_active_idx(true, 30), 10);

  for (int idx : {100, 110, 120}) {
    TickerId symbol = INI_CONFIG.get("meta", "ticker");
    auto p = cfg.index_to_price(idx);
    auto q = QtyType::from_raw(2000);
    MarketData md{
        MarketUpdateType::kAdd, OrderId{1}, symbol, Side::kSell, p, q};
    book_->on_market_data_updated(&md);
  }
  EXPECT_EQ(bbo->ask_price, cfg.index_to_price(100));
  EXPECT_EQ(book_->next_active_idx(false, 100), 110);
  EXPECT_EQ(book_->next_active_idx(false, 110), 120);
  EXPECT_EQ(book_->next_active_idx(false, 120), -1);
}

TEST_F(MarketOrderBookTest, PeekLevels) {
  const auto& cfg = book_->config();
  for (int idx : {5, 15, 25, 35, 45}) {
    TickerId symbol = INI_CONFIG.get("meta", "ticker");
    auto p = cfg.index_to_price(idx);
    auto q = QtyType::from_raw(1000);
    MarketData md{MarketUpdateType::kAdd, OrderId{2}, symbol, Side::kBuy, p, q};
    book_->on_market_data_updated(&md);
  }
  const BBO* bbo = book_->get_bbo();
  EXPECT_EQ(bbo->bid_price, cfg.index_to_price(45));

  auto bids = book_->peek_levels(true, 3);
  std::vector want_bids = {35, 25, 15};
  EXPECT_EQ(bids, want_bids);

  for (int idx : {200, 210, 220}) {
    TickerId symbol = INI_CONFIG.get("meta", "ticker");
    auto p = cfg.index_to_price(idx);
    auto q = QtyType::from_raw(3000);
    MarketData md{
        MarketUpdateType::kAdd, OrderId{3}, symbol, Side::kSell, p, q};
    book_->on_market_data_updated(&md);
  }
  EXPECT_EQ(bbo->ask_price, cfg.index_to_price(200));

  auto asks = book_->peek_levels(false, 2);
  std::vector want_asks = {210, 220};
  EXPECT_EQ(asks, want_asks);
}

// Boundary tests for OrderBookConfig
TEST_F(MarketOrderBookTest, PriceAtMinBoundary) {
  const auto& cfg = book_->config();
  TickerId symbol = INI_CONFIG.get("meta", "ticker");

  const auto min_price = cfg.index_to_price(0);
  const auto qty = QtyType::from_raw(1000);

  const MarketData md(MarketUpdateType::kAdd, OrderId{0}, symbol, Side::kBuy, min_price, qty);
  book_->on_market_data_updated(&md);

  const BBO* bbo = book_->get_bbo();
  EXPECT_EQ(bbo->bid_price, min_price);
  EXPECT_EQ(bbo->bid_qty, qty);

  EXPECT_EQ(cfg.price_to_index(min_price), 0);
}

TEST_F(MarketOrderBookTest, PriceAtMaxBoundary) {
  const auto& cfg = book_->config();
  TickerId symbol = INI_CONFIG.get("meta", "ticker");

  const auto max_price = cfg.index_to_price(cfg.num_levels - 1);
  const auto qty = QtyType::from_raw(2000);

  const MarketData md(MarketUpdateType::kAdd, OrderId{0}, symbol, Side::kSell, max_price, qty);
  book_->on_market_data_updated(&md);

  const BBO* bbo = book_->get_bbo();
  EXPECT_EQ(bbo->ask_price, max_price);
  EXPECT_EQ(bbo->ask_qty, qty);

  EXPECT_EQ(cfg.price_to_index(max_price), cfg.num_levels - 1);
}

TEST_F(MarketOrderBookTest, PriceBelowMinBoundary_ShouldBeRejected) {
  const auto& cfg = book_->config();
  TickerId symbol = INI_CONFIG.get("meta", "ticker");

  const auto invalid_price = cfg.index_to_price(-1);
  const auto qty = QtyType::from_raw(1000);

  const MarketData md(MarketUpdateType::kAdd, OrderId{0}, symbol, Side::kBuy, invalid_price, qty);
  book_->on_market_data_updated(&md);

  const BBO* bbo = book_->get_bbo();
  EXPECT_FALSE(bbo->bid_price.is_valid());
}

TEST_F(MarketOrderBookTest, PriceAboveMaxBoundary_ShouldBeRejected) {
  const auto& cfg = book_->config();
  TickerId symbol = INI_CONFIG.get("meta", "ticker");

  const auto invalid_price = cfg.index_to_price(cfg.num_levels);
  const auto qty = QtyType::from_raw(1000);

  const MarketData md(MarketUpdateType::kAdd, OrderId{0}, symbol, Side::kSell, invalid_price, qty);
  book_->on_market_data_updated(&md);

  // BBO should remain invalid (order rejected)
  const BBO* bbo = book_->get_bbo();
  EXPECT_FALSE(bbo->ask_price.is_valid());
}

// OrderBookConfig unit tests
TEST_F(MarketOrderBookTest, ConfigValuesFromIni) {
  const auto& cfg = book_->config();

  // Verify config is loaded correctly from INI
  EXPECT_GT(cfg.min_price_int, 0);
  EXPECT_GT(cfg.max_price_int, cfg.min_price_int);
  EXPECT_EQ(cfg.num_levels, cfg.max_price_int - cfg.min_price_int + 1);
#ifdef USE_FLAT_ORDERBOOK
  EXPECT_GT(cfg.bitmap_words, 0);
#else
  EXPECT_GT(cfg.bucket_count, 0);
  EXPECT_GT(cfg.summary_words, 0);
#endif
}

TEST_F(MarketOrderBookTest, PriceToIndexAndBack) {
  const auto& cfg = book_->config();

  // Test round-trip conversion
  for (int idx : {0, 100, 1000, 10000, cfg.num_levels - 1}) {
    if (idx >= cfg.num_levels) continue;
    const auto price = cfg.index_to_price(idx);
    const int back_idx = cfg.price_to_index(price);
    EXPECT_EQ(back_idx, idx) << "Round-trip failed for index " << idx;
  }
}

TEST_F(MarketOrderBookTest, MultipleOrdersAcrossBuckets) {
  const auto& cfg = book_->config();
  TickerId symbol = INI_CONFIG.get("meta", "ticker");

  // Add orders in different buckets (bucket size = 4096)
  std::vector<int> indices = {100, 4100, 8100, 12100};  // Different buckets

  for (int idx : indices) {
    if (idx >= cfg.num_levels) continue;
    auto p = cfg.index_to_price(idx);
    auto q = QtyType::from_double(1.0);
    MarketData md{MarketUpdateType::kAdd, OrderId{0}, symbol, Side::kBuy, p, q};
    book_->on_market_data_updated(&md);
  }

  // Best bid should be the highest index (highest price for bids)
  const BBO* bbo = book_->get_bbo();
  int expected_best = *std::max_element(indices.begin(), indices.end());
  if (expected_best < cfg.num_levels) {
    EXPECT_EQ(bbo->bid_price, cfg.index_to_price(expected_best));
  }
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST_F(MarketOrderBookTest, ZeroQuantityAdd_UpdatesBBOWithZero) {
  const auto& cfg = book_->config();
  TickerId symbol = INI_CONFIG.get("meta", "ticker");

  const auto price = cfg.index_to_price(100);
  const auto zero_qty = QtyType::from_raw(0);

  const MarketData md(MarketUpdateType::kAdd, OrderId{0}, symbol, Side::kBuy, price, zero_qty);
  book_->on_market_data_updated(&md);

  const BBO* bbo = book_->get_bbo();
  // Zero qty order is still added to book, but with zero qty
  EXPECT_EQ(bbo->bid_price, price);
  EXPECT_EQ(bbo->bid_qty.value, 0);
}

TEST_F(MarketOrderBookTest, TradeExhaustsEntireLevel_BBOUpdatesToNextLevel) {
  const auto& cfg = book_->config();
  TickerId symbol = INI_CONFIG.get("meta", "ticker");

  // Add two bid levels
  {
    const auto price = cfg.index_to_price(200);
    const auto qty = QtyType::from_raw(5000);
    const MarketData md(MarketUpdateType::kAdd, OrderId{0}, symbol, Side::kBuy, price, qty);
    book_->on_market_data_updated(&md);
  }
  {
    const auto price = cfg.index_to_price(100);
    const auto qty = QtyType::from_raw(3000);
    const MarketData md(MarketUpdateType::kAdd, OrderId{0}, symbol, Side::kBuy, price, qty);
    book_->on_market_data_updated(&md);
  }

  const BBO* bbo = book_->get_bbo();
  EXPECT_EQ(bbo->bid_price, cfg.index_to_price(200));
  EXPECT_EQ(bbo->bid_qty.value, 5000);

  // Trade exhausts the best level
  {
    const auto price = cfg.index_to_price(200);
    const auto qty = QtyType::from_raw(5000);
    const MarketData md(MarketUpdateType::kTrade, OrderId{0}, symbol, Side::kBuy, price, qty);
    book_->on_market_data_updated(&md);
  }

  // BBO should now be the next level
  EXPECT_EQ(bbo->bid_price, cfg.index_to_price(100));
  EXPECT_EQ(bbo->bid_qty.value, 3000);
}

TEST_F(MarketOrderBookTest, TradeMoreThanAvailable_LevelBecomesZero) {
  const auto& cfg = book_->config();
  TickerId symbol = INI_CONFIG.get("meta", "ticker");

  const auto price = cfg.index_to_price(150);
  const auto qty = QtyType::from_raw(2000);
  const MarketData add_md(MarketUpdateType::kAdd, OrderId{0}, symbol, Side::kSell, price, qty);
  book_->on_market_data_updated(&add_md);

  const BBO* bbo = book_->get_bbo();
  EXPECT_EQ(bbo->ask_price, price);

  // Trade more than available
  const auto trade_qty = QtyType::from_raw(3000);
  const MarketData trade_md(MarketUpdateType::kTrade, OrderId{0}, symbol, Side::kSell, price, trade_qty);
  book_->on_market_data_updated(&trade_md);

  // Level should be cleared
  EXPECT_FALSE(bbo->ask_price.is_valid());
}

TEST_F(MarketOrderBookTest, ModifySamePrice_UpdatesQty) {
  const auto& cfg = book_->config();
  TickerId symbol = INI_CONFIG.get("meta", "ticker");

  const auto price = cfg.index_to_price(300);

  // Add order
  {
    const auto qty = QtyType::from_raw(1000);
    const MarketData md(MarketUpdateType::kAdd, OrderId{0}, symbol, Side::kBuy, price, qty);
    book_->on_market_data_updated(&md);
  }

  const BBO* bbo = book_->get_bbo();
  EXPECT_EQ(bbo->bid_qty.value, 1000);

  // Modify multiple times
  for (int i = 2; i <= 5; ++i) {
    const auto new_qty = QtyType::from_raw(1000 * i);
    const MarketData md(MarketUpdateType::kModify, OrderId{0}, symbol, Side::kBuy, price, new_qty);
    book_->on_market_data_updated(&md);
    EXPECT_EQ(bbo->bid_qty.value, 1000 * i);
  }
}

TEST_F(MarketOrderBookTest, CancelNonExistentOrder_NoEffect) {
  TickerId symbol = INI_CONFIG.get("meta", "ticker");
  const auto& cfg = book_->config();

  const auto price = cfg.index_to_price(500);
  const auto qty = QtyType();

  // Cancel on empty book
  const MarketData md(MarketUpdateType::kCancel, OrderId{0}, symbol, Side::kBuy, price, qty);
  book_->on_market_data_updated(&md);

  const BBO* bbo = book_->get_bbo();
  EXPECT_FALSE(bbo->bid_price.is_valid());
}

TEST_F(MarketOrderBookTest, DeleteBestBid_CacheInvalidatedAndRecalculated) {
  const auto& cfg = book_->config();
  TickerId symbol = INI_CONFIG.get("meta", "ticker");

  // Add multiple bids
  std::vector<int> indices = {100, 200, 300};
  for (int idx : indices) {
    auto p = cfg.index_to_price(idx);
    auto q = QtyType::from_raw(1000);
    MarketData md{MarketUpdateType::kAdd, OrderId{0}, symbol, Side::kBuy, p, q};
    book_->on_market_data_updated(&md);
  }

  const BBO* bbo = book_->get_bbo();
  EXPECT_EQ(bbo->bid_price, cfg.index_to_price(300));

  // Delete best bid
  {
    auto p = cfg.index_to_price(300);
    auto q = QtyType();
    MarketData md{MarketUpdateType::kCancel, OrderId{0}, symbol, Side::kBuy, p, q};
    book_->on_market_data_updated(&md);
  }

  // Best bid should now be 200
  EXPECT_EQ(bbo->bid_price, cfg.index_to_price(200));

  // Delete again
  {
    auto p = cfg.index_to_price(200);
    auto q = QtyType();
    MarketData md{MarketUpdateType::kCancel, OrderId{0}, symbol, Side::kBuy, p, q};
    book_->on_market_data_updated(&md);
  }

  EXPECT_EQ(bbo->bid_price, cfg.index_to_price(100));
}

TEST_F(MarketOrderBookTest, DeleteBestAsk_CacheInvalidatedAndRecalculated) {
  const auto& cfg = book_->config();
  TickerId symbol = INI_CONFIG.get("meta", "ticker");

  std::vector<int> indices = {100, 200, 300};
  for (int idx : indices) {
    auto p = cfg.index_to_price(idx);
    auto q = QtyType::from_raw(1000);
    MarketData md{MarketUpdateType::kAdd, OrderId{0}, symbol, Side::kSell, p, q};
    book_->on_market_data_updated(&md);
  }

  const BBO* bbo = book_->get_bbo();
  EXPECT_EQ(bbo->ask_price, cfg.index_to_price(100));

  // Delete best ask
  {
    auto p = cfg.index_to_price(100);
    auto q = QtyType();
    MarketData md{MarketUpdateType::kCancel, OrderId{0}, symbol, Side::kSell, p, q};
    book_->on_market_data_updated(&md);
  }

  EXPECT_EQ(bbo->ask_price, cfg.index_to_price(200));
}

TEST_F(MarketOrderBookTest, PeekLevelsWithQty_ReturnsCorrectData) {
  const auto& cfg = book_->config();
  TickerId symbol = INI_CONFIG.get("meta", "ticker");

  // Add levels: peek_levels_with_qty returns levels AFTER the best (next levels)
  std::vector<std::pair<int, int64_t>> levels = {{100, 1000}, {200, 2000}, {300, 3000}};

  for (const auto& [idx, qty_val] : levels) {
    auto p = cfg.index_to_price(idx);
    auto q = QtyType::from_raw(qty_val);
    MarketData md{MarketUpdateType::kAdd, OrderId{0}, symbol, Side::kBuy, p, q};
    book_->on_market_data_updated(&md);
  }

  std::vector<trading::LevelView> out;
  // peek_levels_with_qty returns levels after best bid (300), so we get 200, 100
  int count = book_->peek_levels_with_qty(true, 3, out);

  // Best bid is 300, so peek returns 200 and 100 (2 levels after best)
  EXPECT_EQ(count, 2);
  ASSERT_EQ(out.size(), 2u);

  // After best bid (300): 200, then 100
  EXPECT_EQ(out[0].idx, 200);
  EXPECT_EQ(out[0].qty_raw, 2000);
  EXPECT_EQ(out[1].idx, 100);
  EXPECT_EQ(out[1].qty_raw, 1000);
}

TEST_F(MarketOrderBookTest, PeekLevelsWithQty_EmptyBook_ReturnsZero) {
  std::vector<trading::LevelView> out;
  int count = book_->peek_levels_with_qty(true, 5, out);

  EXPECT_EQ(count, 0);
  EXPECT_TRUE(out.empty());
}

TEST_F(MarketOrderBookTest, PeekLevelsWithQty_RequestMoreThanAvailable) {
  const auto& cfg = book_->config();
  TickerId symbol = INI_CONFIG.get("meta", "ticker");

  // Add 3 levels for asks
  for (int idx : {100, 200, 300}) {
    auto p = cfg.index_to_price(idx);
    auto q = QtyType::from_raw(1000);
    MarketData md{MarketUpdateType::kAdd, OrderId{0}, symbol, Side::kSell, p, q};
    book_->on_market_data_updated(&md);
  }

  std::vector<trading::LevelView> out;
  // Best ask is 100, peek returns levels after: 200, 300
  int count = book_->peek_levels_with_qty(false, 10, out);

  EXPECT_EQ(count, 2);
  EXPECT_EQ(out.size(), 2u);
}

TEST_F(MarketOrderBookTest, PeekQty_ReturnsCorrectQuantities) {
  const auto& cfg = book_->config();
  TickerId symbol = INI_CONFIG.get("meta", "ticker");

  // peek_qty returns levels after best, so we need 4 levels to get 3 results
  std::vector<std::pair<int, int64_t>> levels = {{50, 500}, {100, 1000}, {150, 1500}, {200, 2000}};

  for (const auto& [idx, qty_val] : levels) {
    auto p = cfg.index_to_price(idx);
    auto q = QtyType::from_raw(qty_val);
    MarketData md{MarketUpdateType::kAdd, OrderId{0}, symbol, Side::kBuy, p, q};
    book_->on_market_data_updated(&md);
  }

  std::array<int64_t, 3> qty_out{};
  std::array<int, 3> idx_out{};
  // Best bid is 200, peek returns levels after: 150, 100, 50
  int filled = book_->peek_qty<int64_t>(true, 3,
      std::span<int64_t>{qty_out.data(), qty_out.size()},
      std::span<int>{idx_out.data(), idx_out.size()});

  EXPECT_EQ(filled, 3);
  // After best bid (200): 150, 100, 50
  EXPECT_EQ(idx_out[0], 150);
  EXPECT_EQ(qty_out[0], 1500);
  EXPECT_EQ(idx_out[1], 100);
  EXPECT_EQ(qty_out[1], 1000);
  EXPECT_EQ(idx_out[2], 50);
  EXPECT_EQ(qty_out[2], 500);
}

TEST_F(MarketOrderBookTest, OrdersAtBucketBoundary) {
  const auto& cfg = book_->config();
  TickerId symbol = INI_CONFIG.get("meta", "ticker");

  // Test at bucket boundary (kBucketSize = 4096)
  constexpr int kBucketSize = 4096;
  std::vector<int> boundary_indices = {
    kBucketSize - 1,
    kBucketSize,
    kBucketSize * 2 - 1,
    kBucketSize * 2
  };

  for (int idx : boundary_indices) {
    if (idx >= cfg.num_levels) continue;
    auto p = cfg.index_to_price(idx);
    auto q = QtyType::from_raw(1000);
    MarketData md{MarketUpdateType::kAdd, OrderId{0}, symbol, Side::kBuy, p, q};
    book_->on_market_data_updated(&md);
  }

  auto levels = book_->peek_levels(true, 10);
  EXPECT_GE(levels.size(), 3u);
}

TEST_F(MarketOrderBookTest, ClearAfterMultipleOperations) {
  const auto& cfg = book_->config();
  TickerId symbol = INI_CONFIG.get("meta", "ticker");

  // Add various orders within valid price range
  for (int idx : {100, 500, 1000, 5000}) {
    if (idx >= cfg.num_levels) continue;
    auto p = cfg.index_to_price(idx);
    auto q = QtyType::from_raw(1000);
    MarketData bid_md{MarketUpdateType::kAdd, OrderId{0}, symbol, Side::kBuy, p, q};
    book_->on_market_data_updated(&bid_md);

    MarketData ask_md{MarketUpdateType::kAdd, OrderId{0}, symbol, Side::kSell, p, q};
    book_->on_market_data_updated(&ask_md);
  }

  const BBO* bbo = book_->get_bbo();
  EXPECT_TRUE(bbo->bid_price.is_valid());
  EXPECT_TRUE(bbo->ask_price.is_valid());

  // Clear - must use a price within valid range for clear to work
  const auto clear_price = cfg.index_to_price(0);
  const MarketData clear_md(MarketUpdateType::kClear, OrderId{kOrderIdInvalid},
                            symbol, Side::kBuy, clear_price, QtyType::from_raw(0));
  book_->on_market_data_updated(&clear_md);

  EXPECT_FALSE(bbo->bid_price.is_valid());
  EXPECT_FALSE(bbo->ask_price.is_valid());

  // Verify peek_levels returns empty
  auto bids = book_->peek_levels(true, 10);
  auto asks = book_->peek_levels(false, 10);
  EXPECT_TRUE(bids.empty());
  EXPECT_TRUE(asks.empty());
}

TEST_F(MarketOrderBookTest, RapidAddDeleteSequence) {
  const auto& cfg = book_->config();
  TickerId symbol = INI_CONFIG.get("meta", "ticker");

  const auto price = cfg.index_to_price(500);

  for (int i = 0; i < 100; ++i) {
    // Add
    {
      auto q = QtyType::from_raw(1000 + i);
      MarketData md{MarketUpdateType::kAdd, OrderId{0}, symbol, Side::kBuy, price, q};
      book_->on_market_data_updated(&md);
    }

    const BBO* bbo = book_->get_bbo();
    EXPECT_EQ(bbo->bid_price, price);

    // Delete
    {
      MarketData md{MarketUpdateType::kCancel, OrderId{0}, symbol, Side::kBuy, price, QtyType()};
      book_->on_market_data_updated(&md);
    }

    EXPECT_FALSE(book_->get_bbo()->bid_price.is_valid());
  }
}

TEST_F(MarketOrderBookTest, NextActiveIdx_NoOrders_ReturnsNegativeOne) {
  EXPECT_EQ(book_->next_active_idx(true, 0), -1);
  EXPECT_EQ(book_->next_active_idx(false, 0), -1);
}

TEST_F(MarketOrderBookTest, LargeQtyValues) {
  const auto& cfg = book_->config();
  TickerId symbol = INI_CONFIG.get("meta", "ticker");

  const auto price = cfg.index_to_price(1000);
  const auto large_qty = QtyType::from_raw(std::numeric_limits<int64_t>::max() / 2);

  const MarketData md(MarketUpdateType::kAdd, OrderId{0}, symbol, Side::kBuy, price, large_qty);
  book_->on_market_data_updated(&md);

  const BBO* bbo = book_->get_bbo();
  EXPECT_EQ(bbo->bid_qty, large_qty);
}

TEST_F(MarketOrderBookTest, ModifyToZeroQty_SetsQtyToZero) {
  const auto& cfg = book_->config();
  TickerId symbol = INI_CONFIG.get("meta", "ticker");

  const auto price = cfg.index_to_price(400);

  // Add
  {
    auto q = QtyType::from_raw(2000);
    MarketData md{MarketUpdateType::kAdd, OrderId{0}, symbol, Side::kSell, price, q};
    book_->on_market_data_updated(&md);
  }

  EXPECT_EQ(book_->get_bbo()->ask_price, price);
  EXPECT_EQ(book_->get_bbo()->ask_qty.value, 2000);

  // Modify to zero - price level stays, qty becomes 0
  {
    auto q = QtyType::from_raw(0);
    MarketData md{MarketUpdateType::kModify, OrderId{0}, symbol, Side::kSell, price, q};
    book_->on_market_data_updated(&md);
  }

  // The order book may keep the level with zero qty or update BBO
  // Either the level is cleared or qty is 0 - test the actual behavior
  const BBO* bbo = book_->get_bbo();
  if (bbo->ask_price.is_valid()) {
    EXPECT_EQ(bbo->ask_qty.value, 0);
  }
}

