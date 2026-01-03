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
    cfg.risk_cfg_.max_order_size_ = QtyType::from_double(10);
    // max_position_ needs to be larger than test fill sizes (45 qty)
    // so that position check passes and we can test loss check
    cfg.risk_cfg_.max_position_ = QtyType::from_double(100);
    // min_position_ defaults to kInvalidValue (MAX_INT64), need to set explicitly
    cfg.risk_cfg_.min_position_ = QtyType::from_double(-100);
    // max_loss_ is compared against total_pnl which is scaled by kPQScale
    // kPQScale = kPriceScale * kQtyScale = 10 * 1000 = 10000
    // So -1000 in original units = -1000 * kPQScale = -10,000,000
    cfg.risk_cfg_.max_loss_ = -10000000;

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
      QtyType::from_double(20),
      QtyType::from_double(0));
  EXPECT_EQ(result, RiskCheckResult::kOrderTooLarge);
}

TEST_F(RiskManagerTest, PositionTooLarge) {
  // Fill with 95 qty so position = 95, then try to add 10 more
  // max_position = 100, so 95 + 10 = 105 > 100 → position too large
  ExecutionReport report;
  report.cl_order_id = OrderId{1};
  report.symbol = INI_CONFIG.get("meta", "ticker");
  report.ord_status = OrdStatus::kFilled;
  report.cum_qty = QtyType::from_double(95.0);
  report.last_qty = QtyType::from_double(95.0);
  report.price = PriceType::from_double(45.0);
  report.side = Side::kBuy;

  keeper_->add_fill(&report);

  auto result = rm_->check_pre_trade_risk(INI_CONFIG.get("meta", "ticker"),
      Side::kBuy,
      QtyType::from_double(10),
      QtyType::from_double(0));
  EXPECT_EQ(result, RiskCheckResult::kPositionTooLarge);
}

TEST_F(RiskManagerTest, LossTooLarge) {
  {
    ExecutionReport report;
    report.cl_order_id = OrderId{1};
    report.symbol = INI_CONFIG.get("meta", "ticker");
    report.ord_status = OrdStatus::kFilled;
    report.cum_qty = QtyType::from_double(45.0);
    report.last_qty = QtyType::from_double(45.0);
    report.price = PriceType::from_double(2000.0);
    report.side = Side::kBuy;
    keeper_->add_fill(&report);
  }
  {
    ExecutionReport report;
    report.cl_order_id = OrderId{2};
    report.symbol = INI_CONFIG.get("meta", "ticker");
    report.ord_status = OrdStatus::kFilled;
    report.cum_qty = QtyType::from_double(45.0);
    report.last_qty = QtyType::from_double(45.0);
    report.price = PriceType::from_double(900.0);
    report.side = Side::kSell;

    keeper_->add_fill(&report);
  }

  auto result = rm_->check_pre_trade_risk(INI_CONFIG.get("meta", "ticker"),
      Side::kBuy,
      QtyType::from_double(5),
      QtyType::from_double(0));
  EXPECT_EQ(result, RiskCheckResult::kLossTooLarge);
}

TEST_F(RiskManagerTest, AllowedTrade) {
  {
    ExecutionReport report;
    report.cl_order_id = OrderId{1};
    report.symbol = INI_CONFIG.get("meta", "ticker");
    report.ord_status = OrdStatus::kFilled;
    report.cum_qty = QtyType::from_double(45.0);
    report.last_qty = QtyType::from_double(45.0);
    report.price = PriceType::from_double(900.0);
    report.side = Side::kBuy;

    keeper_->add_fill(&report);
  }
  {
    ExecutionReport report;
    report.cl_order_id = OrderId{2};
    report.symbol = INI_CONFIG.get("meta", "ticker");
    report.ord_status = OrdStatus::kFilled;
    report.cum_qty = QtyType::from_double(45.0);
    report.last_qty = QtyType::from_double(45.0);
    report.price = PriceType::from_double(9000.0);
    report.side = Side::kSell;

    keeper_->add_fill(&report);
  }
  auto result = rm_->check_pre_trade_risk(INI_CONFIG.get("meta", "ticker"),
      Side::kBuy,
      QtyType::from_double(5),
      QtyType::from_double(0));
  EXPECT_EQ(result, RiskCheckResult::kAllowed);
}