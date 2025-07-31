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

#include "logger.h"
#include "risk_manager.h"  // RiskManager 관련 코드

using namespace trading;
using namespace common;

class RiskManagerTest : public ::testing::Test {
protected:
  PositionKeeper* keeper_;
  Logger* logger_;
  TradeEngineCfgHashMap* ticker_cfg_;
  RiskManager* rm_ = nullptr;

  void SetUp() override {
    logger_ = new Logger();
    keeper_ = new PositionKeeper(logger_);

    TradeEngineCfg cfg;
    cfg.risk_cfg_.max_order_size_ = Qty{10};
    cfg.risk_cfg_.max_position_ = Qty{50};
    cfg.risk_cfg_.max_loss_ = -1000;

    ticker_cfg_ = new TradeEngineCfgHashMap{{"BTCUSDT", cfg}};

    rm_ = new RiskManager(logger_, keeper_, *ticker_cfg_);
  }

  void TearDown() override {
    delete keeper_;
    delete logger_;
    delete ticker_cfg_;
    delete rm_;
  }
};

TEST_F(RiskManagerTest, OrderTooLarge) {
  auto result = rm_->checkPreTradeRisk("BTCUSDT", Side::kBuy, Qty{20});
  EXPECT_EQ(result, RiskCheckResult::kOrderTooLarge);
}

TEST_F(RiskManagerTest, PositionTooLarge) {
  auto* report = new ExecutionReport("1", 1, Price{45}, Qty{45}, Side::kBuy,
                                     "BTCUSDT", OrderStatus::kFilled, Price{45},
                                     Qty{45}, "trade_id1");
  keeper_->add_fill(report);
  delete report;

  auto result = rm_->checkPreTradeRisk("BTCUSDT", Side::kBuy, Qty{10});
  EXPECT_EQ(result, RiskCheckResult::kPositionTooLarge);
}

TEST_F(RiskManagerTest, LossTooLarge) {
  {
    auto* report = new ExecutionReport("1", 1, Price{2000}, Qty{45}, Side::kBuy,
                                       "BTCUSDT", OrderStatus::kFilled,
                                       Price{2000},
                                       Qty{45}, "trade_id1");
    keeper_->add_fill(report);
    delete report;
  }
  {
    auto* report = new ExecutionReport("2", 2, Price{900}, Qty{45}, Side::kSell,
                                       "BTCUSDT", OrderStatus::kFilled,
                                       Price{900},
                                       Qty{45}, "trade_id2");
    keeper_->add_fill(report);
    delete report;
  }

  auto result = rm_->checkPreTradeRisk("BTCUSDT", Side::kBuy, Qty{5});
  EXPECT_EQ(result, RiskCheckResult::kLossTooLarge);
}

TEST_F(RiskManagerTest, AllowedTrade) {
  {
    auto* report = new ExecutionReport("1", 1, Price{900}, Qty{45}, Side::kBuy,
                                       "BTCUSDT", OrderStatus::kFilled,
                                       Price{900},
                                       Qty{45}, "trade_id1");
    keeper_->add_fill(report);
    delete report;
  }
  {
    auto* report = new ExecutionReport("2", 2, Price{9000}, Qty{45},
                                       Side::kSell,
                                       "BTCUSDT", OrderStatus::kFilled,
                                       Price{9000},
                                       Qty{45}, "trade_id2");
    keeper_->add_fill(report);
    delete report;
  }
  auto result = rm_->checkPreTradeRisk("BTCUSDT", Side::kSell, Qty{5});
  EXPECT_EQ(result, RiskCheckResult::kAllowed);
}