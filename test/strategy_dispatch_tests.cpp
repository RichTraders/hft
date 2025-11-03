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
#include "strategy/strategy_dispatch.hpp"
#include "strategy/strategies.hpp"
#include "order_manager.h"
#include "feature_engine.h"
#include "logger.h"
#include "ini_config.hpp"

using namespace trading;
using namespace common;

class MockStrategy : public BaseStrategy {
 public:
  MockStrategy(OrderManager* order_manager, const FeatureEngine* feature_engine,
               Logger* logger, const TradeEngineCfgHashMap&)
      : BaseStrategy(order_manager, feature_engine, logger),
        orderbook_calls_(0),
        trade_calls_(0),
        order_calls_(0) {}

  void on_orderbook_updated(const TickerId&, Price, Side,
                            const MarketOrderBook*) noexcept {
    orderbook_calls_++;
  }

  void on_trade_updated(const MarketData*, MarketOrderBook*) noexcept {
    trade_calls_++;
  }

  void on_order_updated(const ExecutionReport*) noexcept {
    order_calls_++;
  }

  int orderbook_calls_;
  int trade_calls_;
  int order_calls_;
};

namespace {
Registrar<MockStrategy> mock("mock");
}

class StrategyDispatchTest : public ::testing::Test {
 protected:
  static Logger logger_;

  void SetUp() override {
    INI_CONFIG.load("resources/config.ini");
    trading::register_all_strategies();
  }
};

Logger StrategyDispatchTest::logger_;

TEST_F(StrategyDispatchTest, RegistryContainsStrategies) {
  auto& dispatch = StrategyDispatch::instance();
  auto names = dispatch.get_strategy_names();

  EXPECT_GE(names.size(), 3);  
  bool has_maker = false;
  bool has_taker = false;
  bool has_mock = false;

  for (const auto& name : names) {
    if (name == "maker") has_maker = true;
    if (name == "taker") has_taker = true;
    if (name == "mock") has_mock = true;
  }

  EXPECT_TRUE(has_maker) << "MarketMaker strategy not registered";
  EXPECT_TRUE(has_taker) << "LiquidTaker strategy not registered";
  EXPECT_TRUE(has_mock) << "MockStrategy not registered";
}

TEST_F(StrategyDispatchTest, CanRetrieveVTable) {
  auto& dispatch = StrategyDispatch::instance();

  const StrategyVTable* vtable = dispatch.get_vtable("maker");
  ASSERT_NE(vtable, nullptr) << "Failed to get vtable for 'maker'";

  EXPECT_NE(vtable->on_orderbook_updated, nullptr);
  EXPECT_NE(vtable->on_trade_updated, nullptr);
  EXPECT_NE(vtable->on_order_updated, nullptr);
  EXPECT_NE(vtable->create_data, nullptr);
  EXPECT_NE(vtable->destroy_data, nullptr);
}

TEST_F(StrategyDispatchTest, ReturnsNullForInvalidStrategy) {
  auto& dispatch = StrategyDispatch::instance();
  const StrategyVTable* vtable = dispatch.get_vtable("nonexistent");
  EXPECT_EQ(vtable, nullptr);
}

TEST_F(StrategyDispatchTest, CanCreateAndDestroyStrategyData) {
  auto& dispatch = StrategyDispatch::instance();
  const StrategyVTable* vtable = dispatch.get_vtable("mock");
  ASSERT_NE(vtable, nullptr);

  TradeEngineCfgHashMap cfg;
  RiskCfg risk = {.max_order_size_ = Qty{1000.},
                  .max_position_ = Qty{1000.},
                  .max_loss_ = 1000.};
  TradeEngineCfg tempcfg = {
      .clip_ = Qty{100000}, .threshold_ = 10, .risk_cfg_ = risk};
  cfg.emplace("BTCUSDT", tempcfg);

    void* data = vtable->create_data(nullptr, nullptr, &logger_, cfg);
  ASSERT_NE(data, nullptr) << "Failed to create strategy data";

    MockStrategy* strategy = static_cast<MockStrategy*>(data);
  EXPECT_EQ(strategy->orderbook_calls_, 0);
  EXPECT_EQ(strategy->trade_calls_, 0);
  EXPECT_EQ(strategy->order_calls_, 0);

    vtable->destroy_data(data);
}

TEST_F(StrategyDispatchTest, StrategyCallbacksWork) {
  auto& dispatch = StrategyDispatch::instance();
  const StrategyVTable* vtable = dispatch.get_vtable("mock");
  ASSERT_NE(vtable, nullptr);

  TradeEngineCfgHashMap cfg;
  RiskCfg risk = {.max_order_size_ = Qty{1000.},
                  .max_position_ = Qty{1000.},
                  .max_loss_ = 1000.};
  TradeEngineCfg tempcfg = {
      .clip_ = Qty{100000}, .threshold_ = 10, .risk_cfg_ = risk};
  cfg.emplace("BTCUSDT", tempcfg);

  void* data = vtable->create_data(nullptr, nullptr, &logger_, cfg);
  ASSERT_NE(data, nullptr);

  StrategyContext ctx(nullptr, nullptr, &logger_, data);
  MockStrategy* strategy = static_cast<MockStrategy*>(data);

    vtable->on_orderbook_updated(ctx, "BTCUSDT", Price{100.0}, Side::kBuy, nullptr);
  EXPECT_EQ(strategy->orderbook_calls_, 1);

  vtable->on_orderbook_updated(ctx, "BTCUSDT", Price{101.0}, Side::kSell, nullptr);
  EXPECT_EQ(strategy->orderbook_calls_, 2);

    vtable->on_trade_updated(ctx, nullptr, nullptr);
  EXPECT_EQ(strategy->trade_calls_, 1);

    vtable->on_order_updated(ctx, nullptr);
  EXPECT_EQ(strategy->order_calls_, 1);

  vtable->destroy_data(data);
}

TEST_F(StrategyDispatchTest, DifferentStrategiesHaveDifferentVTables) {
  auto& dispatch = StrategyDispatch::instance();

  const StrategyVTable* maker_vtable = dispatch.get_vtable("maker");
  const StrategyVTable* taker_vtable = dispatch.get_vtable("taker");
  const StrategyVTable* mock_vtable = dispatch.get_vtable("mock");

  ASSERT_NE(maker_vtable, nullptr);
  ASSERT_NE(taker_vtable, nullptr);
  ASSERT_NE(mock_vtable, nullptr);

    EXPECT_NE(maker_vtable, taker_vtable);
  EXPECT_NE(maker_vtable, mock_vtable);
  EXPECT_NE(taker_vtable, mock_vtable);

    EXPECT_NE(maker_vtable->on_trade_updated, taker_vtable->on_trade_updated);
  EXPECT_NE(maker_vtable->on_trade_updated, mock_vtable->on_trade_updated);
}

TEST_F(StrategyDispatchTest, StrategyContextLifetimeManagement) {
  auto& dispatch = StrategyDispatch::instance();
  const StrategyVTable* vtable = dispatch.get_vtable("mock");
  ASSERT_NE(vtable, nullptr);

  TradeEngineCfgHashMap cfg;
  RiskCfg risk = {.max_order_size_ = Qty{1000.},
                  .max_position_ = Qty{1000.},
                  .max_loss_ = 1000.};
  TradeEngineCfg tempcfg = {
      .clip_ = Qty{100000}, .threshold_ = 10, .risk_cfg_ = risk};
  cfg.emplace("BTCUSDT", tempcfg);

  void* data = vtable->create_data(nullptr, nullptr, &logger_, cfg);
  ASSERT_NE(data, nullptr);

    StrategyContext ctx1(nullptr, nullptr, &logger_, data);
  StrategyContext ctx2(nullptr, nullptr, &logger_, data);

  MockStrategy* strategy = static_cast<MockStrategy*>(data);

    vtable->on_trade_updated(ctx1, nullptr, nullptr);
  EXPECT_EQ(strategy->trade_calls_, 1);

  vtable->on_trade_updated(ctx2, nullptr, nullptr);
  EXPECT_EQ(strategy->trade_calls_, 2);

  vtable->destroy_data(data);
}

TEST_F(StrategyDispatchTest, FunctionPointerCallOverhead) {
  auto& dispatch = StrategyDispatch::instance();
  const StrategyVTable* vtable = dispatch.get_vtable("mock");
  ASSERT_NE(vtable, nullptr);

  TradeEngineCfgHashMap cfg;
  RiskCfg risk = {.max_order_size_ = Qty{1000.},
                  .max_position_ = Qty{1000.},
                  .max_loss_ = 1000.};
  TradeEngineCfg tempcfg = {
      .clip_ = Qty{100000}, .threshold_ = 10, .risk_cfg_ = risk};
  cfg.emplace("BTCUSDT", tempcfg);

  void* data = vtable->create_data(nullptr, nullptr, &logger_, cfg);
  ASSERT_NE(data, nullptr);

  StrategyContext ctx(nullptr, nullptr, &logger_, data);

    for (int i = 0; i < 1000; ++i) {
    vtable->on_trade_updated(ctx, nullptr, nullptr);
  }

    constexpr int iterations = 1000000;
  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < iterations; ++i) {
    vtable->on_trade_updated(ctx, nullptr, nullptr);
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

  double avg_ns = static_cast<double>(duration.count()) / iterations;

  std::cout << "Average function pointer call overhead: " << avg_ns << " ns\n";
  std::cout << "Total calls: " << iterations << "\n";
  std::cout << "Total time: " << duration.count() / 1000000.0 << " ms\n";

    EXPECT_LT(avg_ns, 10.0) << "Function pointer overhead too high";

  vtable->destroy_data(data);
}

TEST_F(StrategyDispatchTest, StrategyContextStoresCorrectPointers) {
  auto& dispatch = StrategyDispatch::instance();
  const StrategyVTable* vtable = dispatch.get_vtable("mock");
  ASSERT_NE(vtable, nullptr);

  TradeEngineCfgHashMap cfg;
  RiskCfg risk = {.max_order_size_ = Qty{1000.},
                  .max_position_ = Qty{1000.},
                  .max_loss_ = 1000.};
  TradeEngineCfg tempcfg = {
      .clip_ = Qty{100000}, .threshold_ = 10, .risk_cfg_ = risk};
  cfg.emplace("BTCUSDT", tempcfg);

  void* data = vtable->create_data(nullptr, nullptr, &logger_, cfg);
  ASSERT_NE(data, nullptr);

  OrderManager* fake_om = reinterpret_cast<OrderManager*>(0x1234);
  FeatureEngine* fake_fe = reinterpret_cast<FeatureEngine*>(0x5678);

  StrategyContext ctx(fake_om, fake_fe, &logger_, data);

  EXPECT_EQ(ctx.order_manager, fake_om);
  EXPECT_EQ(ctx.feature_engine, fake_fe);
  EXPECT_EQ(ctx.strategy_data, data);

  vtable->destroy_data(data);
}