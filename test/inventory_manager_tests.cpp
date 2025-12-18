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

#include "inventory_manager.h"
#include "inventory_model.h"
#include "position_keeper.h"
#include "order_entry.h"
#include "logger.h"
#include "types.h"

using namespace trading;
using namespace common;

class InventoryManagerTest : public ::testing::Test {
 protected:
  static Logger* logger_;

  static void SetUpTestSuite() {
    INI_CONFIG.load("resources/config.ini");
    logger_ = new Logger();
  }

  static void TearDownTestSuite() { delete logger_; }

  void SetUp() override {
    producer_ = logger_->make_producer();
    position_keeper_ = std::make_unique<PositionKeeper>(producer_);

    // Create empty ticker config
    ticker_cfg_ = TradeEngineCfgHashMap{};
  }

  void TearDown() override {
    inventory_manager_.reset();
    position_keeper_.reset();
  }

  Logger::Producer producer_;
  std::unique_ptr<PositionKeeper> position_keeper_;
  std::unique_ptr<InventoryManager> inventory_manager_;
  TradeEngineCfgHashMap ticker_cfg_;
};

// Static member definition (required!)
Logger* InventoryManagerTest::logger_;

// Test LinearSkewModel directly
TEST(LinearSkewModelTest, ZeroPosition) {
  LinearSkewModel model(0.001);

  // Zero position should result in zero adjustment
  EXPECT_DOUBLE_EQ(0.0, model.calculate_quote_adjustment(Side::kBuy, 0.0, 0.0));
  EXPECT_DOUBLE_EQ(0.0, model.calculate_quote_adjustment(Side::kSell, 0.0, 0.0));
}

TEST(LinearSkewModelTest, LongPosition) {
  LinearSkewModel model(0.001);
  const double position = 100.0;  // Long position
  const double target = 0.0;

  // Long position: bid should be tightened (negative), ask widened (positive)
  const double bid_adj = model.calculate_quote_adjustment(Side::kBuy, position, target);
  const double ask_adj = model.calculate_quote_adjustment(Side::kSell, position, target);

  EXPECT_LT(bid_adj, 0.0);  // Negative adjustment (tighten bid)
  EXPECT_GT(ask_adj, 0.0);  // Positive adjustment (widen ask)
  EXPECT_DOUBLE_EQ(-bid_adj, ask_adj);  // Symmetric
}

TEST(LinearSkewModelTest, ShortPosition) {
  LinearSkewModel model(0.001);
  const double position = -100.0;  // Short position
  const double target = 0.0;

  // Short position: bid should be widened (positive), ask tightened (negative)
  const double bid_adj = model.calculate_quote_adjustment(Side::kBuy, position, target);
  const double ask_adj = model.calculate_quote_adjustment(Side::kSell, position, target);

  EXPECT_GT(bid_adj, 0.0);  // Positive adjustment (widen bid)
  EXPECT_LT(ask_adj, 0.0);  // Negative adjustment (tighten ask)
  EXPECT_DOUBLE_EQ(-bid_adj, ask_adj);  // Symmetric
}

TEST(LinearSkewModelTest, NonZeroTarget) {
  LinearSkewModel model(0.001);
  const double position = 100.0;
  const double target = 50.0;

  // Deviation from target: 100 - 50 = 50
  const double bid_adj = model.calculate_quote_adjustment(Side::kBuy, position, target);
  const double ask_adj = model.calculate_quote_adjustment(Side::kSell, position, target);

  // Expected: skew = 0.001 * 50 = 0.05
  EXPECT_DOUBLE_EQ(-0.05, bid_adj);
  EXPECT_DOUBLE_EQ(0.05, ask_adj);
}

TEST(LinearSkewModelTest, SkewCoefficientScaling) {
  LinearSkewModel model1(0.001);
  LinearSkewModel model2(0.002);
  const double position = 100.0;

  const double adj1 = model1.calculate_quote_adjustment(Side::kBuy, position, 0.0);
  const double adj2 = model2.calculate_quote_adjustment(Side::kBuy, position, 0.0);

  // Double coefficient should double the adjustment
  EXPECT_DOUBLE_EQ(2.0 * adj1, adj2);
}

TEST(LinearSkewModelTest, GetSetCoefficient) {
  LinearSkewModel model(0.001);

  EXPECT_DOUBLE_EQ(0.001, model.get_skew_coefficient());

  model.set_skew_coefficient(0.002);
  EXPECT_DOUBLE_EQ(0.002, model.get_skew_coefficient());
}

// Test InventoryManager integration
TEST_F(InventoryManagerTest, Construction) {
  EXPECT_NO_THROW({
    inventory_manager_ = std::make_unique<InventoryManager>(
        producer_, position_keeper_.get(), ticker_cfg_);
  });
}

TEST_F(InventoryManagerTest, GetQuoteAdjustmentWithZeroPosition) {
  inventory_manager_ = std::make_unique<InventoryManager>(
      producer_, position_keeper_.get(), ticker_cfg_);

  const TickerId ticker = INI_CONFIG.get("meta", "ticker");

  // With no position, adjustment should be zero
  const double bid_adj = inventory_manager_->get_quote_adjustment(Side::kBuy, ticker);
  const double ask_adj = inventory_manager_->get_quote_adjustment(Side::kSell, ticker);

  EXPECT_DOUBLE_EQ(0.0, bid_adj);
  EXPECT_DOUBLE_EQ(0.0, ask_adj);
}

TEST_F(InventoryManagerTest, GetQuoteAdjustmentWithPosition) {
  inventory_manager_ = std::make_unique<InventoryManager>(
      producer_, position_keeper_.get(), ticker_cfg_);

  const TickerId ticker = INI_CONFIG.get("meta", "ticker");

  // Simulate a fill that creates a long position
  ExecutionReport report;
  report.symbol = ticker;
  report.side = Side::kBuy;
  report.last_qty = Qty{100.0};
  report.price = Price{50000.0};
  report.exec_type = ExecType::kTrade;

  position_keeper_->add_fill(&report);

  // Now query adjustments
  const double bid_adj = inventory_manager_->get_quote_adjustment(Side::kBuy, ticker);
  const double ask_adj = inventory_manager_->get_quote_adjustment(Side::kSell, ticker);

  // Long position: bid negative (tighten), ask positive (widen)
  EXPECT_LT(bid_adj, 0.0);
  EXPECT_GT(ask_adj, 0.0);
}

TEST_F(InventoryManagerTest, DynamicCoefficientUpdate) {
  inventory_manager_ = std::make_unique<InventoryManager>(
      producer_, position_keeper_.get(), ticker_cfg_);

  const TickerId ticker = INI_CONFIG.get("meta", "ticker");

  // Create position
  ExecutionReport report;
  report.symbol = ticker;
  report.side = Side::kBuy;
  report.last_qty = Qty{100.0};
  report.price = Price{50000.0};
  report.exec_type = ExecType::kTrade;
  position_keeper_->add_fill(&report);

  const double initial_adj = inventory_manager_->get_quote_adjustment(Side::kBuy, ticker);
  const double initial_coef = inventory_manager_->get_skew_coefficient();

  // Double the coefficient
  inventory_manager_->set_skew_coefficient(initial_coef * 2.0);

  const double new_adj = inventory_manager_->get_quote_adjustment(Side::kBuy, ticker);

  // Adjustment should double
  EXPECT_DOUBLE_EQ(2.0 * initial_adj, new_adj);
}

TEST_F(InventoryManagerTest, PositionReversion) {
  inventory_manager_ = std::make_unique<InventoryManager>(
      producer_, position_keeper_.get(), ticker_cfg_);

  const TickerId ticker = INI_CONFIG.get("meta", "ticker");

  // Go long
  ExecutionReport buy_report;
  buy_report.symbol = ticker;
  buy_report.side = Side::kBuy;
  buy_report.last_qty = Qty{100.0};
  buy_report.price = Price{50000.0};
  buy_report.exec_type = ExecType::kTrade;
  position_keeper_->add_fill(&buy_report);

  const double long_bid_adj = inventory_manager_->get_quote_adjustment(Side::kBuy, ticker);

  // Go short (sell more than long position)
  ExecutionReport sell_report;
  sell_report.symbol = ticker;
  sell_report.side = Side::kSell;
  sell_report.last_qty = Qty{200.0};
  sell_report.price = Price{50000.0};
  sell_report.exec_type = ExecType::kTrade;
  position_keeper_->add_fill(&sell_report);

  const double short_bid_adj = inventory_manager_->get_quote_adjustment(Side::kBuy, ticker);

  // Signs should flip
  EXPECT_LT(long_bid_adj, 0.0);   // Long: negative bid adjustment
  EXPECT_GT(short_bid_adj, 0.0);  // Short: positive bid adjustment
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
