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
#include "logger.h"
#include "position_keeper.h"

#include "ini_config.hpp"
#include "order_book.hpp"
#include "order_entry.h"

using namespace trading;
using namespace common;

// Scale factors for position values
static constexpr int64_t kQtyScale = FixedPointConfig::kQtyScale;
static constexpr int64_t kPriceScale = FixedPointConfig::kPriceScale;
static constexpr int64_t kPQScale = kPriceScale * kQtyScale;  // price * qty scale

class PositionKeeperTest : public ::testing::Test {
 public:
  static Logger* logger;

 protected:
  static void SetUpTestSuite() {
    INI_CONFIG.load("resources/config.ini");
    logger = new Logger();
  }
  void SetUp() override {
    producer = logger->make_producer();
    keeper = new PositionKeeper(producer);
  }
  void TearDown() override {
    if (keeper)
      delete keeper;
  }

  static void TearDownTestSuite() { delete logger; }
  Logger::Producer producer;
  PositionKeeper* keeper;
};
Logger* PositionKeeperTest::logger;

TEST_F(PositionKeeperTest, AddFillIncreasesPosition) {
  ExecutionReport report;
  report.cl_order_id = OrderId{1};
  report.symbol = INI_CONFIG.get("meta", "ticker");
  report.ord_status = OrdStatus::kFilled;
  report.cum_qty = QtyType::from_double(1.0);
  report.last_qty = QtyType::from_double(1.0);
  report.price = PriceType::from_double(100000.0);
  report.side = Side::kBuy;

  keeper->add_fill(&report);

  const PositionInfo* pos_info = keeper->get_position_info(report.symbol);
  ASSERT_NE(pos_info, nullptr);
  EXPECT_EQ(pos_info->get_position(), 1 * kQtyScale);
  EXPECT_EQ(pos_info->volume_, 1 * kQtyScale);
  EXPECT_GE(pos_info->total_pnl_, 0);
}

TEST_F(PositionKeeperTest, AddFill_CrossFlipPosition) {
  PositionInfo pos;
  auto log = logger->make_producer();

  // 1. Long 2 BTC @ 100
  ExecutionReport buy1;
  buy1.cl_order_id = OrderId{1};
  buy1.symbol = INI_CONFIG.get("meta", "ticker");
  buy1.ord_status = OrdStatus::kFilled;
  buy1.cum_qty = QtyType::from_double(2);
  buy1.last_qty = QtyType::from_double(2);
  buy1.price = PriceType::from_double(100);
  buy1.side = Side::kBuy;

  pos.add_fill(&buy1, log);
  EXPECT_EQ(pos.position_, 2 * kQtyScale);
  EXPECT_EQ(pos.real_pnl_, 0);

  // 2. Sell 3 BTC @ 110
  ExecutionReport sell1;
  sell1.cl_order_id = OrderId{2};
  sell1.symbol = INI_CONFIG.get("meta", "ticker");
  sell1.ord_status = OrdStatus::kFilled;
  sell1.cum_qty = QtyType::from_double(3);
  sell1.last_qty = QtyType::from_double(3);
  sell1.price = PriceType::from_double(110);
  sell1.side = Side::kSell;

  pos.add_fill(&sell1, log);

  EXPECT_EQ(pos.position_, -1 * kQtyScale);

  // real_pnl = (110 - 100) * 2 = 20, scaled by kPQScale
  EXPECT_EQ(pos.real_pnl_, 20 * kPQScale);

  // open_vwap = price * qty, so 110 * 1 = 110, scaled by kPQScale
  EXPECT_EQ(pos.open_vwap_[sideToIndex(Side::kSell)], 110 * kPQScale);
  EXPECT_EQ(pos.open_vwap_[sideToIndex(Side::kBuy)], 0);
}

TEST_F(PositionKeeperTest, UnrealPnL_PositiveCase) {
  PositionInfo pos;
  auto log = logger->make_producer();

  // 1. 2 BTC 매수 @ 100
  ExecutionReport buy1;
  buy1.cl_order_id = OrderId{1};
  buy1.symbol = INI_CONFIG.get("meta", "ticker");
  buy1.ord_status = OrdStatus::kFilled;
  buy1.cum_qty = QtyType::from_double(2);
  buy1.last_qty = QtyType::from_double(2);
  buy1.price = PriceType::from_double(100);
  buy1.side = Side::kBuy;

  pos.add_fill(&buy1, log);

  EXPECT_EQ(pos.position_, 2 * kQtyScale);
  EXPECT_EQ(pos.real_pnl_, 0);
  EXPECT_EQ(pos.unreal_pnl_, 0);

  BBO bbo;
  bbo.bid_price = PriceType::from_double(110);
  bbo.ask_price = PriceType::from_double(112);
  pos.update_bbo(&bbo, log);

  // unreal_pnl = (mid - vwap) * position = (111 - 100) * 2 = 22, scaled by kPQScale
  EXPECT_EQ(pos.unreal_pnl_, 22 * kPQScale);
  EXPECT_EQ(pos.total_pnl_, pos.unreal_pnl_ + pos.real_pnl_);
}

TEST_F(PositionKeeperTest, AddFill_AvgPriceCalculation) {
  PositionInfo pos;
  Logger logger;

  auto log = logger.make_producer();
  // 1. Buy 1 BTC @ 100
  ExecutionReport buy1;
  buy1.cl_order_id = OrderId{1};
  buy1.symbol = INI_CONFIG.get("meta", "ticker");
  buy1.ord_status = OrdStatus::kFilled;
  buy1.cum_qty = QtyType::from_double(1);
  buy1.last_qty = QtyType::from_double(1);
  buy1.price = PriceType::from_double(100);
  buy1.side = Side::kBuy;
  pos.add_fill(&buy1, log);

  // 2. Buy 3 BTC @ 110
  ExecutionReport buy2;
  buy2.cl_order_id = OrderId{2};
  buy2.symbol = INI_CONFIG.get("meta", "ticker");
  buy2.ord_status = OrdStatus::kFilled;
  buy2.cum_qty = QtyType::from_double(3);
  buy2.last_qty = QtyType::from_double(3);
  buy2.price = PriceType::from_double(110);
  buy2.side = Side::kBuy;

  pos.add_fill(&buy2, log);

  EXPECT_EQ(pos.position_, 4 * kQtyScale);
  // open_vwap = (1 * 100 + 3 * 110) = 430, scaled by kPQScale
  // vwap = open_vwap / 4 = 107.5
  EXPECT_EQ(pos.open_vwap_[sideToIndex(Side::kBuy)], 430 * kPQScale);
  EXPECT_EQ(pos.real_pnl_, 0);
}

TEST_F(PositionKeeperTest, AddFill_FullCloseRealizesPnL) {
  PositionInfo pos;
  auto log = logger->make_producer();

  // 1. Buy 2 BTC @ 50
  ExecutionReport buy;
  buy.cl_order_id = OrderId{1};
  buy.symbol = INI_CONFIG.get("meta", "ticker");
  buy.ord_status = OrdStatus::kFilled;
  buy.cum_qty = QtyType::from_double(2);
  buy.last_qty = QtyType::from_double(2);
  buy.price = PriceType::from_double(50);
  buy.side = Side::kBuy;
  pos.add_fill(&buy, log);

  // 2. Sell 2 BTC @ 70
  ExecutionReport sell;
  sell.cl_order_id = OrderId{2};
  sell.symbol = INI_CONFIG.get("meta", "ticker");
  sell.ord_status = OrdStatus::kFilled;
  sell.cum_qty = QtyType::from_double(2);
  sell.last_qty = QtyType::from_double(2);
  sell.price = PriceType::from_double(70);
  sell.side = Side::kSell;
  pos.add_fill(&sell, log);

  EXPECT_EQ(pos.position_, 0);
  // real_pnl = (70 - 50) * 2 = 40, scaled by kPQScale
  EXPECT_EQ(pos.real_pnl_, 40 * kPQScale);
  EXPECT_EQ(pos.unreal_pnl_, 0);
}

TEST_F(PositionKeeperTest, UpdateBboUpdatesUnrealPnl) {

  ExecutionReport report;
  report.cl_order_id = OrderId{1};
  report.symbol = INI_CONFIG.get("meta", "ticker");
  report.ord_status = OrdStatus::kFilled;
  report.cum_qty = QtyType::from_double(1.0);
  report.last_qty = QtyType::from_double(1.0);
  report.price = PriceType::from_double(100000.0);
  report.side = Side::kBuy;

  keeper->add_fill(&report);

  BBO bbo;
  bbo.bid_price = PriceType::from_double(101000.0);
  bbo.ask_price = PriceType::from_double(102000.0);

  keeper->update_bbo(report.symbol, &bbo);

  auto pos_info = keeper->get_position_info(report.symbol);
  ASSERT_NE(pos_info, nullptr);
  EXPECT_GT(pos_info->unreal_pnl_, 0);
}

TEST_F(PositionKeeperTest, ToStringPrintsPositions) {

  ExecutionReport report;
  report.cl_order_id = OrderId{1};
  report.symbol = INI_CONFIG.get("meta", "ticker");
  report.ord_status = OrdStatus::kFilled;
  report.cum_qty = QtyType::from_double(1.0);
  report.last_qty = QtyType::from_double(1.0);
  report.price = PriceType::from_double(100000.0);
  report.side = Side::kBuy;

  keeper->add_fill(&report);

  auto output = keeper->toString();
  EXPECT_NE(output.find(INI_CONFIG.get("meta", "ticker")), std::string::npos);
  EXPECT_NE(output.find("pos:"), std::string::npos);
}

// ============ Long Position Side Tests ============

TEST_F(PositionKeeperTest, LongPositionSide_OpenAndClose) {
  PositionInfo pos;
  auto log = logger->make_producer();

  // Open Long: Buy 2 @ 100
  ExecutionReport buy;
  buy.cl_order_id = OrderId{1};
  buy.symbol = INI_CONFIG.get("meta", "ticker");
  buy.ord_status = OrdStatus::kFilled;
  buy.cum_qty = QtyType::from_double(2);
  buy.last_qty = QtyType::from_double(2);
  buy.price = PriceType::from_double(100);
  buy.side = Side::kBuy;
  buy.position_side = PositionSide::kLong;
  pos.add_fill(&buy, log);

  EXPECT_EQ(pos.long_position_raw_, 2 * kQtyScale);
  // long_cost = price * qty = 100 * 2 = 200, scaled by kPQScale
  EXPECT_EQ(pos.long_cost_, 200 * kPQScale);
  EXPECT_EQ(pos.long_real_pnl_, 0);

  // Close Long: Sell 2 @ 120
  ExecutionReport sell;
  sell.cl_order_id = OrderId{2};
  sell.symbol = INI_CONFIG.get("meta", "ticker");
  sell.ord_status = OrdStatus::kFilled;
  sell.cum_qty = QtyType::from_double(2);
  sell.last_qty = QtyType::from_double(2);
  sell.price = PriceType::from_double(120);
  sell.side = Side::kSell;
  sell.position_side = PositionSide::kLong;
  pos.add_fill(&sell, log);

  EXPECT_EQ(pos.long_position_raw_, 0);
  EXPECT_EQ(pos.long_cost_, 0);
  // long_real_pnl = (120 - 100) * 2 = 40, scaled by kPQScale
  EXPECT_EQ(pos.long_real_pnl_, 40 * kPQScale);
}

TEST_F(PositionKeeperTest, LongPositionSide_PartialClose) {
  PositionInfo pos;
  auto log = logger->make_producer();

  // Open Long: Buy 4 @ 100
  ExecutionReport buy;
  buy.cl_order_id = OrderId{1};
  buy.symbol = INI_CONFIG.get("meta", "ticker");
  buy.ord_status = OrdStatus::kFilled;
  buy.cum_qty = QtyType::from_double(4);
  buy.last_qty = QtyType::from_double(4);
  buy.price = PriceType::from_double(100);
  buy.side = Side::kBuy;
  buy.position_side = PositionSide::kLong;
  pos.add_fill(&buy, log);

  // Partial Close Long: Sell 1 @ 110
  ExecutionReport sell;
  sell.cl_order_id = OrderId{2};
  sell.symbol = INI_CONFIG.get("meta", "ticker");
  sell.ord_status = OrdStatus::kFilled;
  sell.cum_qty = QtyType::from_double(1);
  sell.last_qty = QtyType::from_double(1);
  sell.price = PriceType::from_double(110);
  sell.side = Side::kSell;
  sell.position_side = PositionSide::kLong;
  pos.add_fill(&sell, log);

  EXPECT_EQ(pos.long_position_raw_, 3 * kQtyScale);
  // long_cost = 3 * 100 = 300, scaled by kPQScale
  EXPECT_EQ(pos.long_cost_, 300 * kPQScale);
  // long_real_pnl = (110 - 100) * 1 = 10, scaled by kPQScale
  EXPECT_EQ(pos.long_real_pnl_, 10 * kPQScale);
}

// ============ Short Position Side Tests ============

TEST_F(PositionKeeperTest, ShortPositionSide_OpenAndClose) {
  PositionInfo pos;
  auto log = logger->make_producer();

  // Open Short: Sell 2 @ 100
  ExecutionReport sell;
  sell.cl_order_id = OrderId{1};
  sell.symbol = INI_CONFIG.get("meta", "ticker");
  sell.ord_status = OrdStatus::kFilled;
  sell.cum_qty = QtyType::from_double(2);
  sell.last_qty = QtyType::from_double(2);
  sell.price = PriceType::from_double(100);
  sell.side = Side::kSell;
  sell.position_side = PositionSide::kShort;
  pos.add_fill(&sell, log);

  EXPECT_EQ(pos.short_position_raw_, 2 * kQtyScale);
  // short_cost = 100 * 2 = 200, scaled by kPQScale
  EXPECT_EQ(pos.short_cost_, 200 * kPQScale);
  EXPECT_EQ(pos.short_real_pnl_, 0);

  // Close Short: Buy 2 @ 80
  ExecutionReport buy;
  buy.cl_order_id = OrderId{2};
  buy.symbol = INI_CONFIG.get("meta", "ticker");
  buy.ord_status = OrdStatus::kFilled;
  buy.cum_qty = QtyType::from_double(2);
  buy.last_qty = QtyType::from_double(2);
  buy.price = PriceType::from_double(80);
  buy.side = Side::kBuy;
  buy.position_side = PositionSide::kShort;
  pos.add_fill(&buy, log);

  EXPECT_EQ(pos.short_position_raw_, 0);
  EXPECT_EQ(pos.short_cost_, 0);
  // short_real_pnl = (100 - 80) * 2 = 40, scaled by kPQScale
  EXPECT_EQ(pos.short_real_pnl_, 40 * kPQScale);
}

TEST_F(PositionKeeperTest, ShortPositionSide_PartialClose) {
  PositionInfo pos;
  auto log = logger->make_producer();

  // Open Short: Sell 4 @ 100
  ExecutionReport sell;
  sell.cl_order_id = OrderId{1};
  sell.symbol = INI_CONFIG.get("meta", "ticker");
  sell.ord_status = OrdStatus::kFilled;
  sell.cum_qty = QtyType::from_double(4);
  sell.last_qty = QtyType::from_double(4);
  sell.price = PriceType::from_double(100);
  sell.side = Side::kSell;
  sell.position_side = PositionSide::kShort;
  pos.add_fill(&sell, log);

  // Partial Close Short: Buy 1 @ 90
  ExecutionReport buy;
  buy.cl_order_id = OrderId{2};
  buy.symbol = INI_CONFIG.get("meta", "ticker");
  buy.ord_status = OrdStatus::kFilled;
  buy.cum_qty = QtyType::from_double(1);
  buy.last_qty = QtyType::from_double(1);
  buy.price = PriceType::from_double(90);
  buy.side = Side::kBuy;
  buy.position_side = PositionSide::kShort;
  pos.add_fill(&buy, log);

  EXPECT_EQ(pos.short_position_raw_, 3 * kQtyScale);
  // short_cost = 3 * 100 = 300, scaled by kPQScale
  EXPECT_EQ(pos.short_cost_, 300 * kPQScale);
  // short_real_pnl = (100 - 90) * 1 = 10, scaled by kPQScale
  EXPECT_EQ(pos.short_real_pnl_, 10 * kPQScale);
}

// ============ Long/Short Unrealized PnL Tests ============

TEST_F(PositionKeeperTest, LongUnrealizedPnL_UpdateBbo) {
  PositionInfo pos;
  auto log = logger->make_producer();

  // Open Long: Buy 2 @ 100
  ExecutionReport buy;
  buy.cl_order_id = OrderId{1};
  buy.symbol = INI_CONFIG.get("meta", "ticker");
  buy.ord_status = OrdStatus::kFilled;
  buy.cum_qty = QtyType::from_double(2);
  buy.last_qty = QtyType::from_double(2);
  buy.price = PriceType::from_double(100);
  buy.side = Side::kBuy;
  buy.position_side = PositionSide::kLong;
  pos.add_fill(&buy, log);

  // BBO update: mid = 115
  BBO bbo;
  bbo.bid_price = PriceType::from_double(110);
  bbo.ask_price = PriceType::from_double(120);
  pos.update_bbo(&bbo, log);

  // long_unreal_pnl = (115 - 100) * 2 = 30, scaled by kPQScale
  EXPECT_EQ(pos.long_unreal_pnl_, 30 * kPQScale);
}

TEST_F(PositionKeeperTest, ShortUnrealizedPnL_UpdateBbo) {
  PositionInfo pos;
  auto log = logger->make_producer();

  // Open Short: Sell 2 @ 100
  ExecutionReport sell;
  sell.cl_order_id = OrderId{1};
  sell.symbol = INI_CONFIG.get("meta", "ticker");
  sell.ord_status = OrdStatus::kFilled;
  sell.cum_qty = QtyType::from_double(2);
  sell.last_qty = QtyType::from_double(2);
  sell.price = PriceType::from_double(100);
  sell.side = Side::kSell;
  sell.position_side = PositionSide::kShort;
  pos.add_fill(&sell, log);

  // BBO update: mid = 85
  BBO bbo;
  bbo.bid_price = PriceType::from_double(80);
  bbo.ask_price = PriceType::from_double(90);
  pos.update_bbo(&bbo, log);

  // short_unreal_pnl = (100 - 85) * 2 = 30, scaled by kPQScale
  EXPECT_EQ(pos.short_unreal_pnl_, 30 * kPQScale);
}

// ============ Combined Long/Short Tests ============

TEST_F(PositionKeeperTest, HedgeMode_LongAndShortSimultaneous) {
  PositionInfo pos;
  auto log = logger->make_producer();

  // Open Long: Buy 2 @ 100
  ExecutionReport buy_long;
  buy_long.cl_order_id = OrderId{1};
  buy_long.symbol = INI_CONFIG.get("meta", "ticker");
  buy_long.ord_status = OrdStatus::kFilled;
  buy_long.cum_qty = QtyType::from_double(2);
  buy_long.last_qty = QtyType::from_double(2);
  buy_long.price = PriceType::from_double(100);
  buy_long.side = Side::kBuy;
  buy_long.position_side = PositionSide::kLong;
  pos.add_fill(&buy_long, log);

  // Open Short: Sell 1 @ 105
  ExecutionReport sell_short;
  sell_short.cl_order_id = OrderId{2};
  sell_short.symbol = INI_CONFIG.get("meta", "ticker");
  sell_short.ord_status = OrdStatus::kFilled;
  sell_short.cum_qty = QtyType::from_double(1);
  sell_short.last_qty = QtyType::from_double(1);
  sell_short.price = PriceType::from_double(105);
  sell_short.side = Side::kSell;
  sell_short.position_side = PositionSide::kShort;
  pos.add_fill(&sell_short, log);

  EXPECT_EQ(pos.long_position_raw_, 2 * kQtyScale);
  EXPECT_EQ(pos.short_position_raw_, 1 * kQtyScale);
  // net position = 2 - 1 = 1, scaled by kQtyScale
  EXPECT_EQ(pos.position_, 1 * kQtyScale);

  // BBO update: mid = 110
  BBO bbo;
  bbo.bid_price = PriceType::from_double(108);
  bbo.ask_price = PriceType::from_double(112);
  pos.update_bbo(&bbo, log);

  // long_unreal_pnl = (110 - 100) * 2 = 20, scaled by kPQScale
  EXPECT_EQ(pos.long_unreal_pnl_, 20 * kPQScale);
  // short_unreal_pnl = (105 - 110) * 1 = -5, scaled by kPQScale
  EXPECT_EQ(pos.short_unreal_pnl_, -5 * kPQScale);
}

TEST_F(PositionKeeperTest, RealPnL_IsSumOfLongAndShort) {
  PositionInfo pos;
  auto log = logger->make_producer();

  // Long trade: profit 20
  ExecutionReport buy_long;
  buy_long.cl_order_id = OrderId{1};
  buy_long.symbol = INI_CONFIG.get("meta", "ticker");
  buy_long.ord_status = OrdStatus::kFilled;
  buy_long.cum_qty = QtyType::from_double(2);
  buy_long.last_qty = QtyType::from_double(2);
  buy_long.price = PriceType::from_double(100);
  buy_long.side = Side::kBuy;
  buy_long.position_side = PositionSide::kLong;
  pos.add_fill(&buy_long, log);

  ExecutionReport sell_long;
  sell_long.cl_order_id = OrderId{2};
  sell_long.symbol = INI_CONFIG.get("meta", "ticker");
  sell_long.ord_status = OrdStatus::kFilled;
  sell_long.cum_qty = QtyType::from_double(2);
  sell_long.last_qty = QtyType::from_double(2);
  sell_long.price = PriceType::from_double(110);
  sell_long.side = Side::kSell;
  sell_long.position_side = PositionSide::kLong;
  pos.add_fill(&sell_long, log);

  // Short trade: profit 15
  ExecutionReport sell_short;
  sell_short.cl_order_id = OrderId{3};
  sell_short.symbol = INI_CONFIG.get("meta", "ticker");
  sell_short.ord_status = OrdStatus::kFilled;
  sell_short.cum_qty = QtyType::from_double(3);
  sell_short.last_qty = QtyType::from_double(3);
  sell_short.price = PriceType::from_double(100);
  sell_short.side = Side::kSell;
  sell_short.position_side = PositionSide::kShort;
  pos.add_fill(&sell_short, log);

  ExecutionReport buy_short;
  buy_short.cl_order_id = OrderId{4};
  buy_short.symbol = INI_CONFIG.get("meta", "ticker");
  buy_short.ord_status = OrdStatus::kFilled;
  buy_short.cum_qty = QtyType::from_double(3);
  buy_short.last_qty = QtyType::from_double(3);
  buy_short.price = PriceType::from_double(95);
  buy_short.side = Side::kBuy;
  buy_short.position_side = PositionSide::kShort;
  pos.add_fill(&buy_short, log);

  // long_real_pnl = (110 - 100) * 2 = 20, scaled by kPQScale
  EXPECT_EQ(pos.long_real_pnl_, 20 * kPQScale);
  // short_real_pnl = (100 - 95) * 3 = 15, scaled by kPQScale
  EXPECT_EQ(pos.short_real_pnl_, 15 * kPQScale);
  // real_pnl = 20 + 15 = 35, scaled by kPQScale
  EXPECT_EQ(pos.real_pnl_, 35 * kPQScale);
}