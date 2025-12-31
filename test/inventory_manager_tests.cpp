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
// Note: LinearSkewModel uses int64_t positions (scaled by kQtyScale)
// and returns int64_t adjustments (scaled by kPriceScale)
// Use coefficient 0.1 (not 0.001) to work with BTCUSDCConfig's kPriceScale=10
TEST(LinearSkewModelTest, ZeroPosition) {
  LinearSkewModel model(0.1);

  // Zero position should result in zero adjustment
  EXPECT_EQ(0, model.calculate_quote_adjustment(Side::kBuy, 0, 0));
  EXPECT_EQ(0, model.calculate_quote_adjustment(Side::kSell, 0, 0));
}

TEST(LinearSkewModelTest, LongPosition) {
  LinearSkewModel model(0.1);  // 0.1 * kPriceScale(10) = 1 (non-zero)
  // Position 100.0 scaled by kQtyScale
  const int64_t position = 100 * FixedPointConfig::kQtyScale;
  const int64_t target = 0;

  // Long position: bid should be tightened (negative), ask widened (positive)
  const int64_t bid_adj = model.calculate_quote_adjustment(Side::kBuy, position, target);
  const int64_t ask_adj = model.calculate_quote_adjustment(Side::kSell, position, target);

  EXPECT_LT(bid_adj, 0);  // Negative adjustment (tighten bid)
  EXPECT_GT(ask_adj, 0);  // Positive adjustment (widen ask)
  EXPECT_EQ(-bid_adj, ask_adj);  // Symmetric
}

TEST(LinearSkewModelTest, ShortPosition) {
  LinearSkewModel model(0.1);
  // Position -100.0 scaled by kQtyScale
  const int64_t position = -100 * FixedPointConfig::kQtyScale;
  const int64_t target = 0;

  // Short position: bid should be widened (positive), ask tightened (negative)
  const int64_t bid_adj = model.calculate_quote_adjustment(Side::kBuy, position, target);
  const int64_t ask_adj = model.calculate_quote_adjustment(Side::kSell, position, target);

  EXPECT_GT(bid_adj, 0);  // Positive adjustment (widen bid)
  EXPECT_LT(ask_adj, 0);  // Negative adjustment (tighten ask)
  EXPECT_EQ(-bid_adj, ask_adj);  // Symmetric
}

TEST(LinearSkewModelTest, NonZeroTarget) {
  LinearSkewModel model(0.1);
  const int64_t position = 100 * FixedPointConfig::kQtyScale;
  const int64_t target = 50 * FixedPointConfig::kQtyScale;

  // Deviation from target: 100 - 50 = 50
  const int64_t bid_adj = model.calculate_quote_adjustment(Side::kBuy, position, target);
  const int64_t ask_adj = model.calculate_quote_adjustment(Side::kSell, position, target);

  // Expected: skew = 0.1 * 50 = 5.0 (scaled by kPriceScale)
  // skew_scaled = 5.0 * kPriceScale = 50
  const int64_t expected_skew = static_cast<int64_t>(5.0 * FixedPointConfig::kPriceScale);
  EXPECT_EQ(-expected_skew, bid_adj);
  EXPECT_EQ(expected_skew, ask_adj);
}

TEST(LinearSkewModelTest, SkewCoefficientScaling) {
  LinearSkewModel model1(0.1);
  LinearSkewModel model2(0.2);
  const int64_t position = 100 * FixedPointConfig::kQtyScale;

  const int64_t adj1 = model1.calculate_quote_adjustment(Side::kBuy, position, 0);
  const int64_t adj2 = model2.calculate_quote_adjustment(Side::kBuy, position, 0);

  // Double coefficient should double the adjustment
  EXPECT_EQ(2 * adj1, adj2);
}

TEST(LinearSkewModelTest, GetSetCoefficient) {
  // Use coefficient >= 0.1 so it rounds to non-zero with kPriceScale=10
  LinearSkewModel model(0.1);

  EXPECT_DOUBLE_EQ(0.1, model.get_skew_coefficient());

  model.set_skew_coefficient(0.2);
  EXPECT_DOUBLE_EQ(0.2, model.get_skew_coefficient());
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
  const int64_t bid_adj = inventory_manager_->get_quote_adjustment(Side::kBuy, ticker);
  const int64_t ask_adj = inventory_manager_->get_quote_adjustment(Side::kSell, ticker);

  EXPECT_EQ(0, bid_adj);
  EXPECT_EQ(0, ask_adj);
}

TEST_F(InventoryManagerTest, GetQuoteAdjustmentWithPosition) {
  inventory_manager_ = std::make_unique<InventoryManager>(
      producer_, position_keeper_.get(), ticker_cfg_);

  const TickerId ticker = INI_CONFIG.get("meta", "ticker");

  // Set a larger coefficient that works with kPriceScale=10
  // Default 0.001 is too small: 0.001 * 10 = 0 (int64_t truncation)
  inventory_manager_->set_skew_coefficient(0.1);

  // Simulate a fill that creates a long position
  ExecutionReport report;
  report.symbol = ticker;
  report.side = Side::kBuy;
  report.last_qty = QtyType::from_double(100.0);
  report.price = PriceType::from_double(50000.0);
  report.exec_type = ExecType::kTrade;

  position_keeper_->add_fill(&report);

  // Now query adjustments (returns int64_t scaled by kPriceScale)
  const int64_t bid_adj = inventory_manager_->get_quote_adjustment(Side::kBuy, ticker);
  const int64_t ask_adj = inventory_manager_->get_quote_adjustment(Side::kSell, ticker);

  // Long position: bid negative (tighten), ask positive (widen)
  EXPECT_LT(bid_adj, 0);
  EXPECT_GT(ask_adj, 0);
}

TEST_F(InventoryManagerTest, DynamicCoefficientUpdate) {
  inventory_manager_ = std::make_unique<InventoryManager>(
      producer_, position_keeper_.get(), ticker_cfg_);

  const TickerId ticker = INI_CONFIG.get("meta", "ticker");

  // Set a coefficient that works with kPriceScale=10
  inventory_manager_->set_skew_coefficient(0.1);

  // Create position
  ExecutionReport report;
  report.symbol = ticker;
  report.side = Side::kBuy;
  report.last_qty = QtyType::from_double(100.0);
  report.price = PriceType::from_double(50000.0);
  report.exec_type = ExecType::kTrade;
  position_keeper_->add_fill(&report);

  const int64_t initial_adj = inventory_manager_->get_quote_adjustment(Side::kBuy, ticker);
  const double initial_coef = inventory_manager_->get_skew_coefficient();

  // Double the coefficient
  inventory_manager_->set_skew_coefficient(initial_coef * 2.0);

  const int64_t new_adj = inventory_manager_->get_quote_adjustment(Side::kBuy, ticker);

  // Adjustment should double
  EXPECT_EQ(2 * initial_adj, new_adj);
}

TEST_F(InventoryManagerTest, PositionReversion) {
  inventory_manager_ = std::make_unique<InventoryManager>(
      producer_, position_keeper_.get(), ticker_cfg_);

  const TickerId ticker = INI_CONFIG.get("meta", "ticker");

  // Set a coefficient that works with kPriceScale=10
  inventory_manager_->set_skew_coefficient(0.1);

  // Go long
  ExecutionReport buy_report;
  buy_report.symbol = ticker;
  buy_report.side = Side::kBuy;
  buy_report.last_qty = QtyType::from_double(100.0);
  buy_report.price = PriceType::from_double(50000.0);
  buy_report.exec_type = ExecType::kTrade;
  position_keeper_->add_fill(&buy_report);

  const int64_t long_bid_adj = inventory_manager_->get_quote_adjustment(Side::kBuy, ticker);

  // Go short (sell more than long position)
  ExecutionReport sell_report;
  sell_report.symbol = ticker;
  sell_report.side = Side::kSell;
  sell_report.last_qty = QtyType::from_double(200.0);
  sell_report.price = PriceType::from_double(50000.0);
  sell_report.exec_type = ExecType::kTrade;
  position_keeper_->add_fill(&sell_report);

  const int64_t short_bid_adj = inventory_manager_->get_quote_adjustment(Side::kBuy, ticker);

  // Signs should flip
  EXPECT_LT(long_bid_adj, 0);   // Long: negative bid adjustment
  EXPECT_GT(short_bid_adj, 0);  // Short: positive bid adjustment
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
