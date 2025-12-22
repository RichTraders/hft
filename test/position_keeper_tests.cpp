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
  ExecutionReport report{.cl_order_id = OrderId{1},
      .symbol = INI_CONFIG.get("meta", "ticker"),
      .ord_status = OrdStatus::kFilled,
      .cum_qty = Qty{1.0},
      .last_qty = Qty{1.0},
      .price = Price{100000.0},
      .side = Side::kBuy};

  keeper->add_fill(&report);

  const PositionInfo* pos_info = keeper->get_position_info(report.symbol);
  ASSERT_NE(pos_info, nullptr);
  EXPECT_DOUBLE_EQ(pos_info->position_, 1.0);
  EXPECT_DOUBLE_EQ(pos_info->volume_.value, 1.0);
  EXPECT_GE(pos_info->total_pnl_, 0.0);
}

TEST_F(PositionKeeperTest, AddFill_CrossFlipPosition) {
  PositionInfo pos;
  auto log = logger->make_producer();

  // 1. Long 2 BTC @ 100
  ExecutionReport buy1{.cl_order_id = OrderId{1},
      .symbol = INI_CONFIG.get("meta", "ticker"),
      .ord_status = OrdStatus::kFilled,
      .cum_qty = Qty{2},
      .last_qty = Qty{2},
      .price = Price{100},
      .side = Side::kBuy

  };
  pos.add_fill(&buy1, log);
  EXPECT_DOUBLE_EQ(pos.position_, 2.0);
  EXPECT_DOUBLE_EQ(pos.real_pnl_, 0.0);

  // 2. Sell 3 BTC @ 110
  ExecutionReport sell1{
      .cl_order_id = OrderId{2},
      .symbol = INI_CONFIG.get("meta", "ticker"),
      .ord_status = OrdStatus::kFilled,
      .cum_qty = Qty{3},
      .last_qty = Qty{3},
      .price = Price{110},
      .side = Side::kSell,

  };
  pos.add_fill(&sell1, log);

  EXPECT_DOUBLE_EQ(pos.position_, -1.0);

  EXPECT_NEAR(pos.real_pnl_, 20.0, 1e-6);

  EXPECT_NEAR(pos.open_vwap_[sideToIndex(Side::kSell)], 110.0, 1e-6);
  EXPECT_DOUBLE_EQ(pos.open_vwap_[sideToIndex(Side::kBuy)], 0.0);
}

TEST_F(PositionKeeperTest, UnrealPnL_PositiveCase) {
  PositionInfo pos;
  auto log = logger->make_producer();

  // 1. 2 BTC 매수 @ 100
  ExecutionReport buy1{
      .cl_order_id = OrderId{1},
      .symbol = INI_CONFIG.get("meta", "ticker"),
      .ord_status = OrdStatus::kFilled,
      .cum_qty = Qty{2},
      .last_qty = Qty{2},
      .price = Price{100},
      .side = Side::kBuy,

  };
  pos.add_fill(&buy1, log);

  EXPECT_DOUBLE_EQ(pos.position_, 2.0);
  EXPECT_DOUBLE_EQ(pos.real_pnl_, 0.0);
  EXPECT_DOUBLE_EQ(pos.unreal_pnl_, 0.0);

  BBO bbo{.bid_price = Price{110}, .ask_price = Price{112}};
  pos.update_bbo(&bbo, log);

  EXPECT_NEAR(pos.unreal_pnl_, (111.0 - 100.0) * 2.0, 1e-6);
  EXPECT_NEAR(pos.total_pnl_, pos.unreal_pnl_ + pos.real_pnl_, 1e-6);
}

TEST_F(PositionKeeperTest, AddFill_AvgPriceCalculation) {
  PositionInfo pos;
  Logger logger;

  auto log = logger.make_producer();
  // 1. Buy 1 BTC @ 100
  const ExecutionReport buy1{
      .cl_order_id = OrderId{1},
      .symbol = INI_CONFIG.get("meta", "ticker"),
      .ord_status = OrdStatus::kFilled,
      .cum_qty = Qty{1},
      .last_qty = Qty{1},
      .price = Price{100},
      .side = Side::kBuy,
  };
  pos.add_fill(&buy1, log);

  // 2. Buy 3 BTC @ 110
  const ExecutionReport buy2{
      .cl_order_id = OrderId{2},
      .symbol = INI_CONFIG.get("meta", "ticker"),
      .ord_status = OrdStatus::kFilled,
      .cum_qty = Qty{3},
      .last_qty = Qty{3},
      .price = Price{110},
      .side = Side::kBuy,

  };
  pos.add_fill(&buy2, log);

  EXPECT_DOUBLE_EQ(pos.position_, 4.0);
  EXPECT_NEAR(pos.open_vwap_[sideToIndex(Side::kBuy)] / 4.0, 107.5, 1e-6);
  EXPECT_DOUBLE_EQ(pos.real_pnl_, 0.0);
}

TEST_F(PositionKeeperTest, AddFill_FullCloseRealizesPnL) {
  PositionInfo pos;
  auto log = logger->make_producer();

  // 1. Buy 2 BTC @ 50
  ExecutionReport buy{
      .cl_order_id = OrderId{1},
      .symbol = INI_CONFIG.get("meta", "ticker"),
      .ord_status = OrdStatus::kFilled,
      .cum_qty = Qty{2},
      .last_qty = Qty{2},
      .price = Price{50},
      .side = Side::kBuy,
  };
  pos.add_fill(&buy, log);

  // 2. Sell 2 BTC @ 70
  ExecutionReport sell{
      .cl_order_id = OrderId{2},
      .symbol = INI_CONFIG.get("meta", "ticker"),
      .ord_status = OrdStatus::kFilled,
      .cum_qty = Qty{2},
      .last_qty = Qty{2},
      .price = Price{70},
      .side = Side::kSell,
  };
  pos.add_fill(&sell, log);

  EXPECT_DOUBLE_EQ(pos.position_, 0.0);
  EXPECT_NEAR(pos.real_pnl_, 40.0, 1e-6);
  EXPECT_DOUBLE_EQ(pos.unreal_pnl_, 0.0);
}

TEST_F(PositionKeeperTest, UpdateBboUpdatesUnrealPnl) {

  ExecutionReport report{
      .cl_order_id = OrderId{1},
      .symbol = INI_CONFIG.get("meta", "ticker"),
      .ord_status = OrdStatus::kFilled,
      .cum_qty = Qty{1.0},
      .last_qty = Qty{1.0},
      .price = Price{100000.0},
      .side = Side::kBuy,
  };

  keeper->add_fill(&report);

  BBO bbo;
  bbo.bid_price = Price{101000.0};
  bbo.ask_price = Price{102000.0};

  keeper->update_bbo(report.symbol, &bbo);

  auto pos_info = keeper->get_position_info(report.symbol);
  ASSERT_NE(pos_info, nullptr);
  EXPECT_GT(pos_info->unreal_pnl_, 0.0);
}

TEST_F(PositionKeeperTest, ToStringPrintsPositions) {

  ExecutionReport report{
      .cl_order_id = OrderId{1},
      .symbol = INI_CONFIG.get("meta", "ticker"),
      .ord_status = OrdStatus::kFilled,
      .cum_qty = Qty{1.0},
      .last_qty = Qty{1.0},
      .price = Price{100000.0},
      .side = Side::kBuy,
  };

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
  ExecutionReport buy{
      .cl_order_id = OrderId{1},
      .symbol = INI_CONFIG.get("meta", "ticker"),
      .ord_status = OrdStatus::kFilled,
      .cum_qty = Qty{2},
      .last_qty = Qty{2},
      .price = Price{100},
      .side = Side::kBuy,
      .position_side = PositionSide::kLong,
  };
  pos.add_fill(&buy, log);

  EXPECT_DOUBLE_EQ(pos.long_position_, 2.0);
  EXPECT_DOUBLE_EQ(pos.long_cost_, 200.0);
  EXPECT_DOUBLE_EQ(pos.long_real_pnl_, 0.0);

  // Close Long: Sell 2 @ 120
  ExecutionReport sell{
      .cl_order_id = OrderId{2},
      .symbol = INI_CONFIG.get("meta", "ticker"),
      .ord_status = OrdStatus::kFilled,
      .cum_qty = Qty{2},
      .last_qty = Qty{2},
      .price = Price{120},
      .side = Side::kSell,
      .position_side = PositionSide::kLong,
  };
  pos.add_fill(&sell, log);

  EXPECT_DOUBLE_EQ(pos.long_position_, 0.0);
  EXPECT_DOUBLE_EQ(pos.long_cost_, 0.0);
  EXPECT_NEAR(pos.long_real_pnl_, 40.0, 1e-6);  // (120 - 100) * 2
}

TEST_F(PositionKeeperTest, LongPositionSide_PartialClose) {
  PositionInfo pos;
  auto log = logger->make_producer();

  // Open Long: Buy 4 @ 100
  ExecutionReport buy{
      .cl_order_id = OrderId{1},
      .symbol = INI_CONFIG.get("meta", "ticker"),
      .ord_status = OrdStatus::kFilled,
      .cum_qty = Qty{4},
      .last_qty = Qty{4},
      .price = Price{100},
      .side = Side::kBuy,
      .position_side = PositionSide::kLong,
  };
  pos.add_fill(&buy, log);

  // Partial Close Long: Sell 1 @ 110
  ExecutionReport sell{
      .cl_order_id = OrderId{2},
      .symbol = INI_CONFIG.get("meta", "ticker"),
      .ord_status = OrdStatus::kFilled,
      .cum_qty = Qty{1},
      .last_qty = Qty{1},
      .price = Price{110},
      .side = Side::kSell,
      .position_side = PositionSide::kLong,
  };
  pos.add_fill(&sell, log);

  EXPECT_DOUBLE_EQ(pos.long_position_, 3.0);
  EXPECT_NEAR(pos.long_cost_, 300.0, 1e-6);  // 3 * 100
  EXPECT_NEAR(pos.long_real_pnl_, 10.0, 1e-6);  // (110 - 100) * 1
}

// ============ Short Position Side Tests ============

TEST_F(PositionKeeperTest, ShortPositionSide_OpenAndClose) {
  PositionInfo pos;
  auto log = logger->make_producer();

  // Open Short: Sell 2 @ 100
  ExecutionReport sell{
      .cl_order_id = OrderId{1},
      .symbol = INI_CONFIG.get("meta", "ticker"),
      .ord_status = OrdStatus::kFilled,
      .cum_qty = Qty{2},
      .last_qty = Qty{2},
      .price = Price{100},
      .side = Side::kSell,
      .position_side = PositionSide::kShort,
  };
  pos.add_fill(&sell, log);

  EXPECT_DOUBLE_EQ(pos.short_position_, 2.0);
  EXPECT_DOUBLE_EQ(pos.short_cost_, 200.0);
  EXPECT_DOUBLE_EQ(pos.short_real_pnl_, 0.0);

  // Close Short: Buy 2 @ 80
  ExecutionReport buy{
      .cl_order_id = OrderId{2},
      .symbol = INI_CONFIG.get("meta", "ticker"),
      .ord_status = OrdStatus::kFilled,
      .cum_qty = Qty{2},
      .last_qty = Qty{2},
      .price = Price{80},
      .side = Side::kBuy,
      .position_side = PositionSide::kShort,
  };
  pos.add_fill(&buy, log);

  EXPECT_DOUBLE_EQ(pos.short_position_, 0.0);
  EXPECT_DOUBLE_EQ(pos.short_cost_, 0.0);
  EXPECT_NEAR(pos.short_real_pnl_, 40.0, 1e-6);  // (100 - 80) * 2
}

TEST_F(PositionKeeperTest, ShortPositionSide_PartialClose) {
  PositionInfo pos;
  auto log = logger->make_producer();

  // Open Short: Sell 4 @ 100
  ExecutionReport sell{
      .cl_order_id = OrderId{1},
      .symbol = INI_CONFIG.get("meta", "ticker"),
      .ord_status = OrdStatus::kFilled,
      .cum_qty = Qty{4},
      .last_qty = Qty{4},
      .price = Price{100},
      .side = Side::kSell,
      .position_side = PositionSide::kShort,
  };
  pos.add_fill(&sell, log);

  // Partial Close Short: Buy 1 @ 90
  ExecutionReport buy{
      .cl_order_id = OrderId{2},
      .symbol = INI_CONFIG.get("meta", "ticker"),
      .ord_status = OrdStatus::kFilled,
      .cum_qty = Qty{1},
      .last_qty = Qty{1},
      .price = Price{90},
      .side = Side::kBuy,
      .position_side = PositionSide::kShort,
  };
  pos.add_fill(&buy, log);

  EXPECT_DOUBLE_EQ(pos.short_position_, 3.0);
  EXPECT_NEAR(pos.short_cost_, 300.0, 1e-6);  // 3 * 100
  EXPECT_NEAR(pos.short_real_pnl_, 10.0, 1e-6);  // (100 - 90) * 1
}

// ============ Long/Short Unrealized PnL Tests ============

TEST_F(PositionKeeperTest, LongUnrealizedPnL_UpdateBbo) {
  PositionInfo pos;
  auto log = logger->make_producer();

  // Open Long: Buy 2 @ 100
  ExecutionReport buy{
      .cl_order_id = OrderId{1},
      .symbol = INI_CONFIG.get("meta", "ticker"),
      .ord_status = OrdStatus::kFilled,
      .cum_qty = Qty{2},
      .last_qty = Qty{2},
      .price = Price{100},
      .side = Side::kBuy,
      .position_side = PositionSide::kLong,
  };
  pos.add_fill(&buy, log);

  // BBO update: mid = 115
  BBO bbo{.bid_price = Price{110}, .ask_price = Price{120}};
  pos.update_bbo(&bbo, log);

  EXPECT_NEAR(pos.long_unreal_pnl_, 30.0, 1e-6);  // (115 - 100) * 2
}

TEST_F(PositionKeeperTest, ShortUnrealizedPnL_UpdateBbo) {
  PositionInfo pos;
  auto log = logger->make_producer();

  // Open Short: Sell 2 @ 100
  ExecutionReport sell{
      .cl_order_id = OrderId{1},
      .symbol = INI_CONFIG.get("meta", "ticker"),
      .ord_status = OrdStatus::kFilled,
      .cum_qty = Qty{2},
      .last_qty = Qty{2},
      .price = Price{100},
      .side = Side::kSell,
      .position_side = PositionSide::kShort,
  };
  pos.add_fill(&sell, log);

  // BBO update: mid = 85
  BBO bbo{.bid_price = Price{80}, .ask_price = Price{90}};
  pos.update_bbo(&bbo, log);

  EXPECT_NEAR(pos.short_unreal_pnl_, 30.0, 1e-6);  // (100 - 85) * 2
}

// ============ Combined Long/Short Tests ============

TEST_F(PositionKeeperTest, HedgeMode_LongAndShortSimultaneous) {
  PositionInfo pos;
  auto log = logger->make_producer();

  // Open Long: Buy 2 @ 100
  ExecutionReport buy_long{
      .cl_order_id = OrderId{1},
      .symbol = INI_CONFIG.get("meta", "ticker"),
      .ord_status = OrdStatus::kFilled,
      .cum_qty = Qty{2},
      .last_qty = Qty{2},
      .price = Price{100},
      .side = Side::kBuy,
      .position_side = PositionSide::kLong,
  };
  pos.add_fill(&buy_long, log);

  // Open Short: Sell 1 @ 105
  ExecutionReport sell_short{
      .cl_order_id = OrderId{2},
      .symbol = INI_CONFIG.get("meta", "ticker"),
      .ord_status = OrdStatus::kFilled,
      .cum_qty = Qty{1},
      .last_qty = Qty{1},
      .price = Price{105},
      .side = Side::kSell,
      .position_side = PositionSide::kShort,
  };
  pos.add_fill(&sell_short, log);

  EXPECT_DOUBLE_EQ(pos.long_position_, 2.0);
  EXPECT_DOUBLE_EQ(pos.short_position_, 1.0);
  EXPECT_DOUBLE_EQ(pos.position_, 1.0);  // net: 2 - 1 = 1

  // BBO update: mid = 110
  BBO bbo{.bid_price = Price{108}, .ask_price = Price{112}};
  pos.update_bbo(&bbo, log);

  EXPECT_NEAR(pos.long_unreal_pnl_, 20.0, 1e-6);   // (110 - 100) * 2
  EXPECT_NEAR(pos.short_unreal_pnl_, -5.0, 1e-6); // (105 - 110) * 1
}

TEST_F(PositionKeeperTest, RealPnL_IsSumOfLongAndShort) {
  PositionInfo pos;
  auto log = logger->make_producer();

  // Long trade: profit 20
  ExecutionReport buy_long{
      .cl_order_id = OrderId{1},
      .symbol = INI_CONFIG.get("meta", "ticker"),
      .ord_status = OrdStatus::kFilled,
      .cum_qty = Qty{2},
      .last_qty = Qty{2},
      .price = Price{100},
      .side = Side::kBuy,
      .position_side = PositionSide::kLong,
  };
  pos.add_fill(&buy_long, log);

  ExecutionReport sell_long{
      .cl_order_id = OrderId{2},
      .symbol = INI_CONFIG.get("meta", "ticker"),
      .ord_status = OrdStatus::kFilled,
      .cum_qty = Qty{2},
      .last_qty = Qty{2},
      .price = Price{110},
      .side = Side::kSell,
      .position_side = PositionSide::kLong,
  };
  pos.add_fill(&sell_long, log);

  // Short trade: profit 15
  ExecutionReport sell_short{
      .cl_order_id = OrderId{3},
      .symbol = INI_CONFIG.get("meta", "ticker"),
      .ord_status = OrdStatus::kFilled,
      .cum_qty = Qty{3},
      .last_qty = Qty{3},
      .price = Price{100},
      .side = Side::kSell,
      .position_side = PositionSide::kShort,
  };
  pos.add_fill(&sell_short, log);

  ExecutionReport buy_short{
      .cl_order_id = OrderId{4},
      .symbol = INI_CONFIG.get("meta", "ticker"),
      .ord_status = OrdStatus::kFilled,
      .cum_qty = Qty{3},
      .last_qty = Qty{3},
      .price = Price{95},
      .side = Side::kBuy,
      .position_side = PositionSide::kShort,
  };
  pos.add_fill(&buy_short, log);

  EXPECT_NEAR(pos.long_real_pnl_, 20.0, 1e-6);   // (110 - 100) * 2
  EXPECT_NEAR(pos.short_real_pnl_, 15.0, 1e-6); // (100 - 95) * 3
  EXPECT_NEAR(pos.real_pnl_, 35.0, 1e-6);       // 20 + 15
}