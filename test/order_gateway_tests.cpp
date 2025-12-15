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
#include "core/response_manager.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "ini_config.hpp"
#include "logger.h"
#include "order_gateway.h"
#include "strategy_config.hpp"
#include "trade_engine.h"
#include "types.h"

#ifdef USE_FUTURES_API
#include "core/websocket/order_entry/exchanges/binance/futures/binance_futures_oe_traits.h"
using TestOeTraits = BinanceFuturesOeTraits;
#else
#include "core/websocket/order_entry/exchanges/binance/spot/binance_spot_oe_traits.h"
using TestOeTraits = BinanceSpotOeTraits;
#endif

using namespace core;
using namespace common;
using namespace trading;

using TestStrategy = SelectedStrategy<TestOeTraits>;
using TestTradeEngine = TradeEngine<TestStrategy, TestOeTraits>;
using TestOrderGateway = OrderGateway<TestStrategy, TestOeTraits>;

constexpr int cl_order_id = 2075;

class OrderGatewayTest : public ::testing::Test {
 public:
  static std::unique_ptr<Logger> logger;

 protected:
  static void SetUpTestSuite() {
    INI_CONFIG.load("resources/config.ini");
    logger = std::make_unique<Logger>();
    TradeEngineCfgHashMap temp;
    TradeEngineCfg tempcfg;
    temp.emplace(INI_CONFIG.get("meta", "ticker"), tempcfg);
    market_update_data_pool_ =
        std::make_unique<MemoryPool<MarketUpdateData>>(1024);
    market_data_pool_ = std::make_unique<MemoryPool<MarketData>>(1024);

    execution_report_pool_ =
        std::make_unique<MemoryPool<ExecutionReport>>(1024);
    order_cancel_reject_pool_ =
        std::make_unique<MemoryPool<OrderCancelReject>>(1024);
    order_mass_cancel_report_pool_ =
        std::make_unique<MemoryPool<OrderMassCancelReport>>(1024);

    response_manager_ = std::make_unique<ResponseManager>(logger.get(),
        execution_report_pool_.get(),
        order_cancel_reject_pool_.get(),
        order_mass_cancel_report_pool_.get());

    order_gateway_ = std::make_unique<TestOrderGateway>(logger.get(),
        response_manager_.get());
    trade_engine_ = std::make_unique<TestTradeEngine>(logger.get(),
        market_update_data_pool_.get(),
        market_data_pool_.get(),
        response_manager_.get(),
        temp);

    order_gateway_->init_trade_engine(trade_engine_.get());
    trade_engine_->init_order_gateway(order_gateway_.get());
  }
  static void TearDownTestSuite() {
    order_gateway_->stop();
    sleep(3);
    std::cout << "TearDown OrderGatewayTest" << std::endl;
  }

 public:
  static std::unique_ptr<MemoryPool<MarketUpdateData>> market_update_data_pool_;
  static std::unique_ptr<MemoryPool<MarketData>> market_data_pool_;

  static std::unique_ptr<MemoryPool<ExecutionReport>> execution_report_pool_;
  static std::unique_ptr<MemoryPool<OrderCancelReject>>
      order_cancel_reject_pool_;
  static std::unique_ptr<MemoryPool<OrderMassCancelReport>>
      order_mass_cancel_report_pool_;
  static std::unique_ptr<ResponseManager> response_manager_;
  static std::unique_ptr<TestTradeEngine> trade_engine_;
  static std::unique_ptr<TestOrderGateway> order_gateway_;
};
std::unique_ptr<Logger> OrderGatewayTest::logger;

TEST_F(OrderGatewayTest, NewOrderSingle) {
  RequestCommon request;

  request.req_type = ReqeustType::kNewSingleOrderData;
  request.cl_order_id.value = cl_order_id;
  request.symbol = INI_CONFIG.get("meta", "ticker");
  request.side = common::Side::kSell;
  request.order_qty.value = 0.01;
  request.price.value = 120000;
  request.ord_type = trading::OrderType::kLimit;
  request.time_in_force = trading::TimeInForce::kGoodTillCancel;
  request.self_trade_prevention_mode =
      trading::SelfTradePreventionMode::kExpireTaker;

  trade_engine_->send_request(request);

  sleep(3);
}

TEST_F(OrderGatewayTest, OrderCancel) {
  RequestCommon request;

  request.req_type = ReqeustType::kOrderCancelRequest;
  request.cl_order_id.value = cl_order_id + 1;
  request.orig_cl_order_id.value = cl_order_id;
  request.symbol = INI_CONFIG.get("meta", "ticker");

  trade_engine_->send_request(request);

  sleep(3);
}

TEST_F(OrderGatewayTest, DISABLED_OrderMassCancel) {
  auto logger = std::make_unique<Logger>();

  TradeEngineCfgHashMap temp;
  TradeEngineCfg tempcfg;
  temp.emplace(INI_CONFIG.get("meta", "ticker"), tempcfg);
  auto pool = std::make_unique<MemoryPool<MarketUpdateData>>(1024);
  auto pool2 = std::make_unique<MemoryPool<MarketData>>(1024);

  auto execution_report_pool =
      std::make_unique<MemoryPool<ExecutionReport>>(1024);
  auto order_cancel_reject_pool =
      std::make_unique<MemoryPool<OrderCancelReject>>(1024);
  auto order_mass_cancel_report_pool =
      std::make_unique<MemoryPool<OrderMassCancelReport>>(1024);

  auto response_manager = std::make_unique<ResponseManager>(logger.get(),
      execution_report_pool.get(),
      order_cancel_reject_pool.get(),
      order_mass_cancel_report_pool.get());
  TestOrderGateway og(logger.get(), response_manager.get());
  auto trade_engine = new TestTradeEngine(logger.get(),
      pool.get(),
      pool2.get(),
      response_manager.get(),
      temp);
  og.init_trade_engine(trade_engine);
  trade_engine->init_order_gateway(&og);

  RequestCommon request;

  request.req_type = ReqeustType::kOrderMassCancelRequest;
  request.cl_order_id.value = cl_order_id;
  request.symbol = INI_CONFIG.get("meta", "ticker");

  sleep(2);

  trade_engine->send_request(request);

  sleep(3);
}

// stop 구현 필요

std::unique_ptr<MemoryPool<MarketUpdateData>>
    OrderGatewayTest::market_update_data_pool_;
std::unique_ptr<MemoryPool<MarketData>> OrderGatewayTest::market_data_pool_;

std::unique_ptr<MemoryPool<ExecutionReport>>
    OrderGatewayTest::execution_report_pool_;
std::unique_ptr<MemoryPool<OrderCancelReject>>
    OrderGatewayTest::order_cancel_reject_pool_;
std::unique_ptr<MemoryPool<OrderMassCancelReport>>
    OrderGatewayTest::order_mass_cancel_report_pool_;
std::unique_ptr<ResponseManager> OrderGatewayTest::response_manager_;
std::unique_ptr<TestTradeEngine> OrderGatewayTest::trade_engine_;
std::unique_ptr<TestOrderGateway> OrderGatewayTest::order_gateway_;
