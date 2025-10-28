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

#include "gtest/gtest.h"
#include "ini_config.hpp"
#include "logger.h"
#include "response_manager.h"
#include "strategy/strategies.hpp"
#include "trade_engine.h"

using namespace trading;
using namespace common;

class StrategyIntegrationTest : public ::testing::Test {
 protected:
  static std::unique_ptr<Logger> logger;
  void SetUp() override {
    INI_CONFIG.load("resources/config.ini");
    register_all_strategies();
    logger =std::make_unique<Logger>();
  }
};
std::unique_ptr<Logger> StrategyIntegrationTest::logger;

TEST_F(StrategyIntegrationTest, TradeEngineLoadsStrategyFromConfig) {
  TradeEngineCfgHashMap cfg;
  RiskCfg risk = {.max_order_size_ = Qty{0.0001},
                  .max_position_ = Qty{0.0004},
                  .max_loss_ = -0.3};
  TradeEngineCfg tempcfg = {
      .clip_ = Qty{100000}, .threshold_ = 10, .risk_cfg_ = risk};
  cfg.emplace("BTCUSDT", tempcfg);

  {

    auto market_update_pool = std::make_unique<MemoryPool<MarketUpdateData>>(64);
    auto market_data_pool = std::make_unique<MemoryPool<MarketData>>(32768);

    auto execution_report_pool =
        std::make_unique<MemoryPool<ExecutionReport>>(1024);
    auto order_cancel_reject_pool =
        std::make_unique<MemoryPool<OrderCancelReject>>(1024);
    auto order_mass_cancel_report_pool =
        std::make_unique<MemoryPool<OrderMassCancelReport>>(1024);

    auto response_manager = std::make_unique<ResponseManager>(
        logger.get(), execution_report_pool.get(), order_cancel_reject_pool.get(),
        order_mass_cancel_report_pool.get());

    EXPECT_NO_THROW({
      auto trade_engine = std::make_unique<TradeEngine>(
          logger.get(), market_update_pool.get(), market_data_pool.get(),
          response_manager.get(), cfg);
      trade_engine->stop();
    });
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));
}

TEST_F(StrategyIntegrationTest, TradeEngineThrowsOnInvalidStrategy) {
  INI_CONFIG.set("strategy", "algorithm", "invalid_strategy_name");

  TradeEngineCfgHashMap cfg;
  RiskCfg risk = {.max_order_size_ = Qty{0.0001},
                  .max_position_ = Qty{0.0004},
                  .max_loss_ = -0.3};
  TradeEngineCfg tempcfg = {
      .clip_ = Qty{100000}, .threshold_ = 10, .risk_cfg_ = risk};
  cfg.emplace("BTCUSDT", tempcfg);

  auto market_update_pool = std::make_unique<MemoryPool<MarketUpdateData>>(64);
  auto market_data_pool = std::make_unique<MemoryPool<MarketData>>(32768);

  auto execution_report_pool =
      std::make_unique<MemoryPool<ExecutionReport>>(1024);
  auto order_cancel_reject_pool =
      std::make_unique<MemoryPool<OrderCancelReject>>(1024);
  auto order_mass_cancel_report_pool =
      std::make_unique<MemoryPool<OrderMassCancelReport>>(1024);

  auto response_manager = std::make_unique<ResponseManager>(
      logger.get(), execution_report_pool.get(), order_cancel_reject_pool.get(),
      order_mass_cancel_report_pool.get());

  EXPECT_THROW(
      {
        auto trade_engine = std::make_unique<TradeEngine>(
            logger.get(), market_update_pool.get(), market_data_pool.get(),
            response_manager.get(), cfg);
        trade_engine->stop();
      },
      std::runtime_error);
  INI_CONFIG.set("strategy", "algorithm", "maker");
}

TEST_F(StrategyIntegrationTest, CanSwitchStrategiesByConfig) {
  INI_CONFIG.set("strategy", "algorithm", "maker");
  {
    TradeEngineCfgHashMap cfg;
    RiskCfg risk = {.max_order_size_ = Qty{0.0001},
                    .max_position_ = Qty{0.0004},
                    .max_loss_ = -0.3};
    TradeEngineCfg tempcfg = {
        .clip_ = Qty{100000}, .threshold_ = 10, .risk_cfg_ = risk};
    cfg.emplace("BTCUSDT", tempcfg);

    //auto logger = std::make_unique<Logger>();
    auto market_update_pool =
        std::make_unique<MemoryPool<MarketUpdateData>>(64);
    auto market_data_pool = std::make_unique<MemoryPool<MarketData>>(32768);

    auto execution_report_pool =
        std::make_unique<MemoryPool<ExecutionReport>>(1024);
    auto order_cancel_reject_pool =
        std::make_unique<MemoryPool<OrderCancelReject>>(1024);
    auto order_mass_cancel_report_pool =
        std::make_unique<MemoryPool<OrderMassCancelReport>>(1024);

    auto response_manager = std::make_unique<ResponseManager>(
        logger.get(), execution_report_pool.get(),
        order_cancel_reject_pool.get(), order_mass_cancel_report_pool.get());

    EXPECT_NO_THROW({
      auto te1 = std::make_unique<TradeEngine>(
          logger.get(), market_update_pool.get(), market_data_pool.get(),
          response_manager.get(), cfg);
      te1->stop();
    });
  }

  INI_CONFIG.set("strategy", "algorithm", "taker");
  {
    TradeEngineCfgHashMap cfg;
    RiskCfg risk = {.max_order_size_ = Qty{0.0001},
                    .max_position_ = Qty{0.0004},
                    .max_loss_ = -0.3};
    TradeEngineCfg tempcfg = {
        .clip_ = Qty{100000}, .threshold_ = 10, .risk_cfg_ = risk};
    cfg.emplace("BTCUSDT", tempcfg);

    auto market_update_pool2 =
        std::make_unique<MemoryPool<MarketUpdateData>>(64);
    auto market_data_pool2 = std::make_unique<MemoryPool<MarketData>>(32768);

    auto execution_report_pool2 =
        std::make_unique<MemoryPool<ExecutionReport>>(1024);
    auto order_cancel_reject_pool2 =
        std::make_unique<MemoryPool<OrderCancelReject>>(1024);
    auto order_mass_cancel_report_pool2 =
        std::make_unique<MemoryPool<OrderMassCancelReport>>(1024);

    auto response_manager2 = std::make_unique<ResponseManager>(
        logger.get(), execution_report_pool2.get(),
        order_cancel_reject_pool2.get(), order_mass_cancel_report_pool2.get());
    EXPECT_NO_THROW({
      auto te2 = std::make_unique<TradeEngine>(
          logger.get(), market_update_pool2.get(), market_data_pool2.get(),
          response_manager2.get(), cfg);
      te2->stop();
    });
  }

  INI_CONFIG.set("strategy", "algorithm", "maker");
}
