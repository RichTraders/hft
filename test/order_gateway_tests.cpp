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
#include "../hft/core/NewOroFix44/response_manager.h"
#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "order_gateway.h"
#include "ini_config.hpp"
#include "logger.h"
#include "fix_oe_app.h"
#include "trade_engine.h"
#include "types.h"
#include <fix8/f8includes.hpp>

using namespace core;
using namespace common;
using namespace trading;

int cl_order_id = 2075;

class OrderGatewayTest : public ::testing::Test  {
protected:

  static void SetUpTestSuite() {
    IniConfig config;
    config.load("resources/config.ini");
    const Authorization authorization{
      .md_address = config.get("auth", "md_address"),
      .oe_address = config.get("auth", "oe_address"),
      .port = config.get_int("auth", "port"),
      .api_key = config.get("auth", "api_key"),
      .pem_file_path = config.get("auth", "pem_file_path"),
      .private_password = config.get("auth", "private_password")};

    auto logger = std::make_unique<Logger>();

    TradeEngineCfgHashMap temp;
    TradeEngineCfg tempcfg;
    temp.emplace("BTCUSDT", tempcfg);
    market_update_data_pool_ = std::make_unique<MemoryPool<MarketUpdateData>>(1024);
    market_data_pool_ = std::make_unique<MemoryPool<MarketData>>(1024);

    execution_report_pool_ = std::make_unique<MemoryPool<
        ExecutionReport>>(1024);
    order_cancel_reject_pool_ = std::make_unique<MemoryPool<
      OrderCancelReject>>(1024);
    order_mass_cancel_report_pool_ = std::make_unique<MemoryPool<
      OrderMassCancelReport>>(1024);

    response_manager_ = std::make_unique<ResponseManager>(
        logger.get(), execution_report_pool_.get(), order_cancel_reject_pool_.get(),
        order_mass_cancel_report_pool_.get());

    order_gateway_= std::make_unique<trading::OrderGateway>(authorization, logger.get(), response_manager_.get());
    trade_engine_ = std::make_unique<TradeEngine>(logger.get(), market_update_data_pool_.get(),
                                                 market_data_pool_.get(), response_manager_.get(), temp);

    order_gateway_->init_trade_engine(trade_engine_.get());
    trade_engine_->init_order_gateway(order_gateway_.get());
  }
  static void TearDownTestSuite() {
    order_gateway_->stop();
    sleep(3);
    std::cout << "TearDown OrderGatewayTest" << std::endl;
  }

public:
  static std::unique_ptr<MemoryPool<
          MarketUpdateData>> market_update_data_pool_;
  static std::unique_ptr<MemoryPool<
        MarketData>> market_data_pool_;

  static std::unique_ptr<MemoryPool<
        ExecutionReport>> execution_report_pool_;
  static std::unique_ptr<MemoryPool<
        OrderCancelReject>> order_cancel_reject_pool_;
  static std::unique_ptr<MemoryPool<
        OrderMassCancelReport>> order_mass_cancel_report_pool_;
  static std::unique_ptr<ResponseManager> response_manager_;
  static std::unique_ptr<TradeEngine> trade_engine_;
  static std::unique_ptr<OrderGateway> order_gateway_;

};

TEST_F(OrderGatewayTest, NewOrderSingle) {
  RequestCommon request;

  request.req_type = ReqeustType::kNewSingleOrderData;
  request.cl_order_id.value = cl_order_id;
  request.symbol = "BTCUSDT";
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
  request.symbol = "BTCUSDT";

  trade_engine_->send_request(request);

  sleep(3);
}

TEST_F(OrderGatewayTest, DISABLED_OrderMassCancel) {
  IniConfig config;
  config.load("resources/config.ini");
  const Authorization authorization{
      .md_address = config.get("auth", "md_address"),
      .oe_address = config.get("auth", "oe_address"),
      .port = config.get_int("auth", "port"),
      .api_key = config.get("auth", "api_key"),
      .pem_file_path = config.get("auth", "pem_file_path"),
      .private_password = config.get("auth", "private_password")};

  auto logger = std::make_unique<Logger>();

  TradeEngineCfgHashMap temp;
  TradeEngineCfg tempcfg;
  temp.emplace("BTCUSDT", tempcfg);
  auto pool = std::make_unique<MemoryPool<MarketUpdateData>>(1024);
  auto pool2 = std::make_unique<MemoryPool<MarketData>>(1024);

  auto execution_report_pool = std::make_unique<MemoryPool<
    ExecutionReport>>(1024);
  auto order_cancel_reject_pool = std::make_unique<MemoryPool<
    OrderCancelReject>>(1024);
  auto order_mass_cancel_report_pool = std::make_unique<MemoryPool<
    OrderMassCancelReport>>(1024);

  auto response_manager = std::make_unique<ResponseManager>(
      logger.get(), execution_report_pool.get(), order_cancel_reject_pool.get(),
      order_mass_cancel_report_pool.get());
  OrderGateway og(authorization, logger.get(), response_manager.get());
  auto trade_engine = new TradeEngine(logger.get(), pool.get(),
                                               pool2.get(), response_manager.get(), temp);
  og.init_trade_engine(trade_engine);
  trade_engine->init_order_gateway(&og);

  RequestCommon request;

  request.req_type = ReqeustType::kOrderMassCancelRequest;
  request.cl_order_id.value = cl_order_id;
  request.symbol = "BTCUSDT";

  sleep(2);

  trade_engine->send_request(request);

  sleep(3);
}

// stop 구현 필요


std::unique_ptr<MemoryPool<
        MarketUpdateData>> OrderGatewayTest::market_update_data_pool_;
std::unique_ptr<MemoryPool<
      MarketData>> OrderGatewayTest::market_data_pool_;

std::unique_ptr<MemoryPool<
      ExecutionReport>> OrderGatewayTest::execution_report_pool_;
std::unique_ptr<MemoryPool<
      OrderCancelReject>> OrderGatewayTest::order_cancel_reject_pool_;
std::unique_ptr<MemoryPool<
      OrderMassCancelReport>> OrderGatewayTest::order_mass_cancel_report_pool_;
std::unique_ptr<ResponseManager> OrderGatewayTest::response_manager_;
std::unique_ptr<TradeEngine> OrderGatewayTest::trade_engine_;
std::unique_ptr<OrderGateway> OrderGatewayTest::order_gateway_;