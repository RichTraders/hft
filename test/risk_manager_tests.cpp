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
#include <order_entry.h>
#include "common/logger.h"
#include "ini_config.hpp"
#include "risk_manager.h"
#include "src/position_keeper.h"

using namespace trading;
using namespace common;

class RiskManagerTest : public ::testing::Test {
 public:
  static Logger* logger;
  static std::unique_ptr<Logger::Producer> producer;

 protected:
  PositionKeeper* keeper_;

  TradeEngineCfgHashMap* ticker_cfg_;
  RiskManager* rm_ = nullptr;

  static void SetUpTestSuite() {
    logger = new Logger();
    producer = std::make_unique<Logger::Producer>(logger->make_producer());
  }

  void SetUp() override {
    INI_CONFIG.load("resources/config.ini");

    keeper_ = new PositionKeeper(*producer);

    TradeEngineCfg cfg;
    cfg.risk_cfg_.max_order_size_ = Qty{10};
    cfg.risk_cfg_.max_position_ = Qty{9};
    cfg.risk_cfg_.max_loss_ = -1000;

    ticker_cfg_ =
        new TradeEngineCfgHashMap{{INI_CONFIG.get("meta", "ticker"), cfg}};

    rm_ = new RiskManager(*producer, keeper_, *ticker_cfg_);
  }
  static void TearDownTestSuite() { delete logger; }

  void TearDown() override {
    delete keeper_;
    delete ticker_cfg_;
    delete rm_;
  }
};
Logger* RiskManagerTest::logger;
std::unique_ptr<Logger::Producer> RiskManagerTest::producer;

TEST_F(RiskManagerTest, OrderTooLarge) {
  auto result = rm_->check_pre_trade_risk(INI_CONFIG.get("meta", "ticker"),
      Side::kBuy,
      Qty{20},
      Qty{0});
  EXPECT_EQ(result, RiskCheckResult::kOrderTooLarge);
}

TEST_F(RiskManagerTest, PositionTooLarge) {
  ExecutionReport report{
      .cl_order_id = OrderId{1},
      .symbol = INI_CONFIG.get("meta", "ticker"),
      .ord_status = OrdStatus::kFilled,
      .cum_qty = Qty{45.0},
      .last_qty = Qty{45.0},
      .price = Price{45.0},
      .side = Side::kBuy,
  };

  keeper_->add_fill(&report);

  auto result = rm_->check_pre_trade_risk(INI_CONFIG.get("meta", "ticker"),
      Side::kBuy,
      Qty{10},
      Qty{0});
  EXPECT_EQ(result, RiskCheckResult::kPositionTooLarge);
}

TEST_F(RiskManagerTest, LossTooLarge) {
  {
    ExecutionReport report{
        .cl_order_id = OrderId{1},
        .symbol = INI_CONFIG.get("meta", "ticker"),
        .ord_status = OrdStatus::kFilled,
        .cum_qty = Qty{45.0},
        .last_qty = Qty{45.0},
        .price = Price{2000.0},
        .side = Side::kBuy,
    };
    keeper_->add_fill(&report);
  }
  {
    ExecutionReport report{
        .cl_order_id = OrderId{2},
        .symbol = INI_CONFIG.get("meta", "ticker"),
        .ord_status = OrdStatus::kFilled,
        .cum_qty = Qty{45.0},
        .last_qty = Qty{45.0},
        .price = Price{900.0},
        .side = Side::kSell,
    };

    keeper_->add_fill(&report);
  }

  auto result = rm_->check_pre_trade_risk(INI_CONFIG.get("meta", "ticker"),
      Side::kBuy,
      Qty{5},
      Qty{0});
  EXPECT_EQ(result, RiskCheckResult::kLossTooLarge);
}

TEST_F(RiskManagerTest, AllowedTrade) {
  {
    ExecutionReport report{
        .cl_order_id = OrderId{1},
        .symbol = INI_CONFIG.get("meta", "ticker"),
        .ord_status = OrdStatus::kFilled,
        .cum_qty = Qty{45.0},
        .last_qty = Qty{45.0},
        .price = Price{900.0},
        .side = Side::kBuy,
    };

    keeper_->add_fill(&report);
  }
  {
    ExecutionReport report{
        .cl_order_id = OrderId{2},
        .symbol = INI_CONFIG.get("meta", "ticker"),
        .ord_status = OrdStatus::kFilled,
        .cum_qty = Qty{45.0},
        .last_qty = Qty{45.0},
        .price = Price{9000.0},
        .side = Side::kSell,
    };

    keeper_->add_fill(&report);
  }
  auto result = rm_->check_pre_trade_risk(INI_CONFIG.get("meta", "ticker"),
      Side::kBuy,
      Qty{5},
      Qty{0});
  EXPECT_EQ(result, RiskCheckResult::kAllowed);
}