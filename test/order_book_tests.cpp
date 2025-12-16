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
    // Load BTCUSDT config before creating order book
    INI_CONFIG.load("resources/config.ini");

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
  const auto& cfg = book_->config();
  for (int idx : {10, 20, 30}) {
    TickerId symbol = INI_CONFIG.get("meta", "ticker");
    Price p =
        Price{static_cast<double>(cfg.min_price_int + idx) / cfg.tick_multiplier_int};
    Qty q{1.0};
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
    Price p =
        Price{static_cast<double>(cfg.min_price_int + idx) / cfg.tick_multiplier_int};
    Qty q{2.0};
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
    Price p =
        Price{static_cast<double>(cfg.min_price_int + idx) / cfg.tick_multiplier_int};
    Qty q{1.0};
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
    Price p =
        Price{static_cast<double>(cfg.min_price_int + idx) / cfg.tick_multiplier_int};
    Qty q{1.0};
    MarketData md{
        MarketUpdateType::kCancel, OrderId{0}, symbol, Side::kBuy, p, q};
    book_->on_market_data_updated(&md);
  }
  EXPECT_EQ(bbo->bid_price, cfg.index_to_price(30));
  EXPECT_EQ(book_->next_active_idx(true, 30), 10);

  for (int idx : {100, 110, 120}) {
    TickerId symbol = INI_CONFIG.get("meta", "ticker");
    Price p =
        Price{static_cast<double>(cfg.min_price_int + idx) / cfg.tick_multiplier_int};
    Qty q{2.0};
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
    Price p =
        Price{static_cast<double>(cfg.min_price_int + idx) / cfg.tick_multiplier_int};
    Qty q{1.0};
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
    Price p =
        Price{static_cast<double>(cfg.min_price_int + idx) / cfg.tick_multiplier_int};
    Qty q{3.0};
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

  // min_price_int = 100000, tick_multiplier_int = 100
  // min valid price = 100000 / 100 = 1000.00
  const double min_price_val = static_cast<double>(cfg.min_price_int) / cfg.tick_multiplier_int;
  const Price min_price = Price{min_price_val};
  const Qty qty = Qty{1.0};

  const MarketData md(MarketUpdateType::kAdd, OrderId{0}, symbol, Side::kBuy, min_price, qty);
  book_->on_market_data_updated(&md);

  const BBO* bbo = book_->get_bbo();
  EXPECT_EQ(bbo->bid_price, min_price);
  EXPECT_EQ(bbo->bid_qty, qty);

  // Index should be 0 at min price
  EXPECT_EQ(cfg.price_to_index(min_price), 0);
}

TEST_F(MarketOrderBookTest, PriceAtMaxBoundary) {
  const auto& cfg = book_->config();
  TickerId symbol = INI_CONFIG.get("meta", "ticker");

  // max_price_int = 30000000, tick_multiplier_int = 100
  // max valid price = 30000000 / 100 = 300000.00
  const double max_price_val = static_cast<double>(cfg.max_price_int) / cfg.tick_multiplier_int;
  const Price max_price = Price{max_price_val};
  const Qty qty = Qty{2.0};

  const MarketData md(MarketUpdateType::kAdd, OrderId{0}, symbol, Side::kSell, max_price, qty);
  book_->on_market_data_updated(&md);

  const BBO* bbo = book_->get_bbo();
  EXPECT_EQ(bbo->ask_price, max_price);
  EXPECT_EQ(bbo->ask_qty, qty);

  // Index should be num_levels - 1 at max price
  EXPECT_EQ(cfg.price_to_index(max_price), cfg.num_levels - 1);
}

TEST_F(MarketOrderBookTest, PriceBelowMinBoundary_ShouldBeRejected) {
  const auto& cfg = book_->config();
  TickerId symbol = INI_CONFIG.get("meta", "ticker");

  // Price below minimum should be rejected
  const double below_min = static_cast<double>(cfg.min_price_int - 1) / cfg.tick_multiplier_int;
  const Price invalid_price = Price{below_min};
  const Qty qty = Qty{1.0};

  const MarketData md(MarketUpdateType::kAdd, OrderId{0}, symbol, Side::kBuy, invalid_price, qty);
  book_->on_market_data_updated(&md);

  // BBO should remain invalid (order rejected)
  const BBO* bbo = book_->get_bbo();
  EXPECT_EQ(bbo->bid_price, kPriceInvalid);
}

TEST_F(MarketOrderBookTest, PriceAboveMaxBoundary_ShouldBeRejected) {
  const auto& cfg = book_->config();
  TickerId symbol = INI_CONFIG.get("meta", "ticker");

  // Price above maximum should be rejected
  const double above_max = static_cast<double>(cfg.max_price_int + 1) / cfg.tick_multiplier_int;
  const Price invalid_price = Price{above_max};
  const Qty qty = Qty{1.0};

  const MarketData md(MarketUpdateType::kAdd, OrderId{0}, symbol, Side::kSell, invalid_price, qty);
  book_->on_market_data_updated(&md);

  // BBO should remain invalid (order rejected)
  const BBO* bbo = book_->get_bbo();
  EXPECT_EQ(bbo->ask_price, kPriceInvalid);
}

// OrderBookConfig unit tests
TEST_F(MarketOrderBookTest, ConfigValuesFromIni) {
  const auto& cfg = book_->config();

  // Verify config is loaded correctly from INI
  EXPECT_GT(cfg.min_price_int, 0);
  EXPECT_GT(cfg.max_price_int, cfg.min_price_int);
  EXPECT_GT(cfg.tick_multiplier_int, 0);
  EXPECT_EQ(cfg.num_levels, cfg.max_price_int - cfg.min_price_int + 1);
  EXPECT_GT(cfg.bucket_count, 0);
  EXPECT_GT(cfg.summary_words, 0);
}

TEST_F(MarketOrderBookTest, PriceToIndexAndBack) {
  const auto& cfg = book_->config();

  // Test round-trip conversion
  for (int idx : {0, 100, 1000, 10000, cfg.num_levels - 1}) {
    if (idx >= cfg.num_levels) continue;
    const Price price = cfg.index_to_price(idx);
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
    Price p = cfg.index_to_price(idx);
    Qty q{1.0};
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
// XRPUSDC Symbol Tests - Different price range and tick multiplier
// ============================================================================
class XrpusdcOrderBookTest : public ::testing::Test {
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
    // Load XRPUSDC config before creating order book
    INI_CONFIG.load("resources/config-xrpusdc.ini");

    TradeEngineCfgHashMap temp;
    RiskCfg risk = {.max_order_size_ = Qty{1000.},
                    .max_position_ = Qty{5000.},
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
std::unique_ptr<Logger> XrpusdcOrderBookTest::logger;
std::unique_ptr<Logger::Producer> XrpusdcOrderBookTest::producer;

TEST_F(XrpusdcOrderBookTest, ConfigLoadedCorrectly) {
  const auto& cfg = book_->config();

  // XRPUSDC config: min=1000, max=1000000, tick_mult=10000
  // Price range: $0.10 ~ $100.00
  EXPECT_EQ(cfg.min_price_int, 1000);
  EXPECT_EQ(cfg.max_price_int, 1000000);
  EXPECT_EQ(cfg.tick_multiplier_int, 10000);
  EXPECT_EQ(cfg.num_levels, 1000000 - 1000 + 1);
}

TEST_F(XrpusdcOrderBookTest, PriceConversionForXrpusdc) {
  const auto& cfg = book_->config();

  // $0.50 = 5000 / 10000 -> index = 5000 - 1000 = 4000
  Price price_050{0.50};
  EXPECT_EQ(cfg.price_to_index(price_050), 4000);
  EXPECT_EQ(cfg.index_to_price(4000), price_050);

  // $1.00 = 10000 / 10000 -> index = 10000 - 1000 = 9000
  Price price_100{1.00};
  EXPECT_EQ(cfg.price_to_index(price_100), 9000);
  EXPECT_EQ(cfg.index_to_price(9000), price_100);

  // Min price: $0.10 = 1000 / 10000 -> index = 0
  Price min_price{0.10};
  EXPECT_EQ(cfg.price_to_index(min_price), 0);

  // Max price: $100.00 = 1000000 / 10000 -> index = 999000
  Price max_price{100.00};
  EXPECT_EQ(cfg.price_to_index(max_price), cfg.num_levels - 1);
}

TEST_F(XrpusdcOrderBookTest, AddOrderAtXrpusdcPriceRange) {
  TickerId symbol = INI_CONFIG.get("meta", "ticker");

  // Add order at $0.50
  const Price price = Price{0.50};
  const Qty qty = Qty{100.0};
  const MarketData md(MarketUpdateType::kAdd, OrderId{0}, symbol, Side::kBuy, price, qty);
  book_->on_market_data_updated(&md);

  const BBO* bbo = book_->get_bbo();
  EXPECT_EQ(bbo->bid_price, price);
  EXPECT_EQ(bbo->bid_qty, qty);
}

TEST_F(XrpusdcOrderBookTest, XrpusdcBoundaryPrices) {
  const auto& cfg = book_->config();
  TickerId symbol = INI_CONFIG.get("meta", "ticker");

  // Add at min price $0.10
  {
    const Price min_price = Price{0.10};
    const MarketData md(MarketUpdateType::kAdd, OrderId{0}, symbol, Side::kBuy, min_price, Qty{50.0});
    book_->on_market_data_updated(&md);

    const BBO* bbo = book_->get_bbo();
    EXPECT_EQ(bbo->bid_price, min_price);
    EXPECT_EQ(cfg.price_to_index(min_price), 0);
  }

  // Add at max price $100.00 (sell side)
  {
    const Price max_price = Price{100.00};
    const MarketData md(MarketUpdateType::kAdd, OrderId{1}, symbol, Side::kSell, max_price, Qty{25.0});
    book_->on_market_data_updated(&md);

    const BBO* bbo = book_->get_bbo();
    EXPECT_EQ(bbo->ask_price, max_price);
    EXPECT_EQ(cfg.price_to_index(max_price), cfg.num_levels - 1);
  }
}

TEST_F(XrpusdcOrderBookTest, XrpusdcPriceBelowMin_ShouldBeRejected) {
  TickerId symbol = INI_CONFIG.get("meta", "ticker");

  // $0.09 is below min ($0.10)
  const Price invalid_price = Price{0.09};
  const MarketData md(MarketUpdateType::kAdd, OrderId{0}, symbol, Side::kBuy, invalid_price, Qty{10.0});
  book_->on_market_data_updated(&md);

  const BBO* bbo = book_->get_bbo();
  EXPECT_EQ(bbo->bid_price, kPriceInvalid);
}

TEST_F(XrpusdcOrderBookTest, XrpusdcPriceAboveMax_ShouldBeRejected) {
  TickerId symbol = INI_CONFIG.get("meta", "ticker");

  // $100.01 is above max ($100.00)
  const Price invalid_price = Price{100.01};
  const MarketData md(MarketUpdateType::kAdd, OrderId{0}, symbol, Side::kSell, invalid_price, Qty{10.0});
  book_->on_market_data_updated(&md);

  const BBO* bbo = book_->get_bbo();
  EXPECT_EQ(bbo->ask_price, kPriceInvalid);
}
