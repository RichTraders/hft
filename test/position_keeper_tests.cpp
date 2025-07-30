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
#include "position_keeper.h"
#include "logger.h"

#include "order_book.h"

using namespace trading;
using namespace common;

class PositionKeeperTest : public ::testing::Test {
protected:
  void SetUp() override {
    logger = new Logger();
    keeper = new PositionKeeper(logger);
  }
  void TearDown() override {
    delete logger;
    delete keeper;
  }

  Logger* logger;
  PositionKeeper* keeper;

};

TEST_F(PositionKeeperTest, AddFillIncreasesPosition) {
  ExecutionReport report{
      .execution_id = "1",
      .order_id = 100,
      .price = Price{100000.0},
      .qty = Qty{1.0},
      .side = Side::kBuy,
      .symbol = "BTCUSDT",
      .order_status = OrderStatus::kFilled,
      .last_price = Price{100000.0},
      .last_qty = Qty{1.0},
      .trade_id = "T1"
  };

  keeper->add_fill(&report);

  const PositionInfo* pos_info = keeper->get_position_info(report.symbol);
  ASSERT_NE(pos_info, nullptr);
  EXPECT_DOUBLE_EQ(pos_info->position_, 1.0);
  EXPECT_DOUBLE_EQ(pos_info->volume_.value, 1.0);
  EXPECT_GE(pos_info->total_pnl_, 0.0); // 초기 체결은 PnL 0
}

TEST_F(PositionKeeperTest, AddFill_CrossFlipPosition) {
  PositionInfo pos;

  // 1. Long 2 BTC @ 100
  ExecutionReport buy1{
    .execution_id = "1",
    .order_id = 1,
    .price = Price{100},
    .qty = Qty{2},
    .side = Side::kBuy,
    .symbol = "BTCUSDT",
    .order_status = OrderStatus::kFilled,
    .last_price = Price{100},
    .last_qty = Qty{2},
    .trade_id = "T1"};
  pos.add_fill(&buy1, logger);
  EXPECT_DOUBLE_EQ(pos.position_, 2.0);
  EXPECT_DOUBLE_EQ(pos.real_pnl_, 0.0);

  // 2. Sell 3 BTC @ 110 (롱 2 BTC 청산 + 1 BTC 숏 전환)
  ExecutionReport sell1{
    .execution_id = "2",
    .order_id = 2,
    .price = Price{110},
    .qty = Qty{3},
    .side = Side::kSell,
    .symbol = "BTCUSDT",
    .order_status = OrderStatus::kFilled,
    .last_price = Price{110},
    .last_qty = Qty{3},
    .trade_id = "T2"};
  pos.add_fill(&sell1, logger);

  EXPECT_DOUBLE_EQ(pos.position_, -1.0);

  EXPECT_NEAR(pos.real_pnl_, 20.0, 1e-6);

  EXPECT_NEAR(pos.open_vwap_[sideToIndex(Side::kSell)], 110.0, 1e-6);
  EXPECT_DOUBLE_EQ(pos.open_vwap_[sideToIndex(Side::kBuy)], 0.0);
}

TEST_F(PositionKeeperTest, UnrealPnL_PositiveCase) {
  PositionInfo pos;

  // 1. 2 BTC 매수 @ 100
  ExecutionReport buy1{
    .execution_id = "1",
    .order_id = 1,
    .price = Price{100},
    .qty = Qty{2},
    .side = Side::kBuy,
    .symbol = "BTCUSDT",
    .order_status = OrderStatus::kFilled,
    .last_price = Price{100},
    .last_qty = Qty{2},
    .trade_id = "T1"};
  pos.add_fill(&buy1, logger);

  EXPECT_DOUBLE_EQ(pos.position_, 2.0);
  EXPECT_DOUBLE_EQ(pos.real_pnl_, 0.0);
  EXPECT_DOUBLE_EQ(pos.unreal_pnl_, 0.0);

  // 2. BBO 업데이트 (bid=110, ask=112) → mid=111
  BBO bbo{
    .bid_price = Price{110},
    .ask_price = Price{112}};
  pos.update_bbo(&bbo, logger);

  // 미실현 손익 = (현재가(mid=111) - 진입 VWAP 100) * 2 BTC
  EXPECT_NEAR(pos.unreal_pnl_, (111.0 - 100.0) * 2.0, 1e-6);

  // 총손익 = 미실현 + 실현
  EXPECT_NEAR(pos.total_pnl_, pos.unreal_pnl_ + pos.real_pnl_, 1e-6);
}


TEST_F(PositionKeeperTest, AddFill_AvgPriceCalculation) {
  PositionInfo pos;
  Logger logger;

  // 1. Buy 1 BTC @ 100
  const ExecutionReport buy1{
    .execution_id = "1",
    .order_id = 1,
    .price = Price{100},
    .qty = Qty{1},
    .side = Side::kBuy,
    .symbol = "BTCUSDT",
    .order_status = OrderStatus::kFilled,
    .last_price = Price{100},
    .last_qty = Qty{1},
    .trade_id = "T1"
};
  pos.add_fill(&buy1, &logger);

  // 2. Buy 3 BTC @ 110
  const ExecutionReport buy2{
    .execution_id = "2",
    .order_id = 2,
    .price = Price{110},
    .qty = Qty{3},
    .side = Side::kBuy,
    .symbol = "BTCUSDT",
    .order_status = OrderStatus::kFilled,
    .last_price = Price{110},
    .last_qty = Qty{3},
    .trade_id = "T2"
};
  pos.add_fill(&buy2, &logger);

  EXPECT_DOUBLE_EQ(pos.position_, 4.0);
  EXPECT_NEAR(pos.open_vwap_[sideToIndex(Side::kBuy)] / 4.0, 107.5, 1e-6);
  EXPECT_DOUBLE_EQ(pos.real_pnl_, 0.0);
}

TEST_F(PositionKeeperTest, AddFill_FullCloseRealizesPnL) {
  PositionInfo pos;

  // 1. Buy 2 BTC @ 50
  ExecutionReport buy{
    .execution_id = "1",
    .order_id = 1,
    .price = Price{50},
    .qty = Qty{2},
    .side = Side::kBuy,
    .symbol = "BTCUSDT",
    .order_status = OrderStatus::kFilled,
    .last_price = Price{50},
    .last_qty = Qty{2},
    .trade_id = "T1"
};
  pos.add_fill(&buy, logger);

  // 2. Sell 2 BTC @ 70 (포지션 완전 청산)
  ExecutionReport sell{
    .execution_id = "2",
    .order_id = 2,
    .price = Price{70},
    .qty = Qty{2},
    .side = Side::kSell,
    .symbol = "BTCUSDT",
    .order_status = OrderStatus::kFilled,
    .last_price = Price{70},
    .last_qty = Qty{2},
    .trade_id = "T2"
};
  pos.add_fill(&sell, logger);

  EXPECT_DOUBLE_EQ(pos.position_, 0.0);
  EXPECT_NEAR(pos.real_pnl_, 40.0, 1e-6);
  EXPECT_DOUBLE_EQ(pos.unreal_pnl_, 0.0);
}

TEST_F(PositionKeeperTest, UpdateBboUpdatesUnrealPnl) {

  ExecutionReport report{
      .execution_id = "1",
      .order_id = 100,
      .price = Price{100000.0},
      .qty = Qty{1.0},
      .side = Side::kBuy,
      .symbol = "BTCUSDT",
      .order_status = OrderStatus::kFilled,
      .last_price = Price{100000.0},
      .last_qty = Qty{1.0},
      .trade_id = "T1"
  };

  keeper->add_fill(&report);

  BBO bbo;
  bbo.bid_price = Price{101000.0};
  bbo.ask_price = Price{102000.0};

  keeper->update_bbo(report.symbol, &bbo);

  auto pos_info = keeper->get_position_info(report.symbol);
  ASSERT_NE(pos_info, nullptr);
  EXPECT_GT(pos_info->unreal_pnl_, 0.0); // 매수 후 가격 상승 → 평가손익 +
}

TEST_F(PositionKeeperTest, ToStringPrintsPositions) {

  ExecutionReport report{
      .execution_id = "1",
      .order_id = 100,
      .price = Price{100000.0},
      .qty = Qty{1.0},
      .side = Side::kBuy,
      .symbol = "BTCUSDT",
      .order_status = OrderStatus::kFilled,
      .last_price = Price{100000.0},
      .last_qty = Qty{1.0},
      .trade_id = "T1"
  };

  keeper->add_fill(&report);

  auto output = keeper->toString();
  EXPECT_NE(output.find("BTCUSDT"), std::string::npos);
  EXPECT_NE(output.find("pos:"), std::string::npos);
}