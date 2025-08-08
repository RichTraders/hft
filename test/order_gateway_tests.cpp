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

TEST(OrderGatewayTest, DISABLED_NewOrderSingle) {
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

  TradeEngine* trade_engine_ = new TradeEngine(logger.get(), pool.get(),
                                               pool2.get(), temp);
  OrderGateway og(authorization, logger.get(), trade_engine_);

  RequestCommon request;
  std::string name_order = "Neworo_order_gateway_test_4";

  request.req_type = ReqeustType::kNewSingleOrderData;
  request.order_name = name_order;
  request.symbol = "BTCUSDT";
  request.side = common::Side::kBuy;
  request.order_qty = 0.01;
  request.price = 190000;
  request.ord_type = trading::OrderType::kLimit;
  request.time_in_force = trading::TimeInForce::kGoodTillCancel;
  request.self_trade_prevention_mode =
      trading::SelfTradePreventionMode::kExpireTaker;

  sleep(2);

  og.order_request(request);

  sleep(2);

  ResponseCommon response;
  response = trade_engine_->dequeue_response();
  EXPECT_EQ(response.res_type, ResponseType::kInvalid);

  if (response.res_type == ResponseType::kExecutionReport) {
    EXPECT_FALSE(
        response.execution_report->cl_ord_id.compare(name_order
        ));
  }

  sleep(10);
}


TEST(OrderGatewayTest, OrderCancel) {
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

  TradeEngine* trade_engine_ = new TradeEngine(logger.get(), pool.get(),
                                               pool2.get(), temp);
  OrderGateway og(authorization, logger.get(), trade_engine_);

  RequestCommon request;
  std::string name_order = "Neworo_order_gateway_test_12";// 계속 바꿔줘야됨

  request.req_type = ReqeustType::kNewSingleOrderData;
  request.order_name = name_order;
  request.symbol = "BTCUSDT";
  request.side = common::Side::kBuy;
  request.order_qty = 0.01;
  request.price = 116000.0;
  request.ord_type = trading::OrderType::kLimit;
  request.time_in_force = trading::TimeInForce::kGoodTillCancel;
  request.self_trade_prevention_mode =
      trading::SelfTradePreventionMode::kExpireTaker;

  sleep(2);

  og.order_request(request);

  sleep(2);

  ResponseCommon response;
  response = trade_engine_->dequeue_response();
  EXPECT_EQ(response.res_type, ResponseType::kInvalid);

  if (response.res_type == ResponseType::kExecutionReport) {
    EXPECT_FALSE(
        response.execution_report->cl_ord_id.compare(name_order
        ));


    ASSERT_LE(response.execution_report->ord_status, trading::OrdStatus::kFilled);

    RequestCommon request;
    std::string name_order = "Neworo_order_gateway_test_cancel_12";// 계속 바꿔줘야됨

    request.req_type = ReqeustType::kOrderCancelRequest;
    request.order_name = name_order;
    request.cl_order_id = response.execution_report->order_id;
    request.symbol = "BTCUSDT";

    sleep(2);

    og.order_request(request);

    sleep(2);

    ResponseCommon response_cancel;
    int cnt = 10;
    while (cnt--) {
      response_cancel = trade_engine_->dequeue_response();

      if (response_cancel.res_type == ResponseType::kExecutionReport) {
        sleep(2);
      }
    }
  }

  sleep(5);
}

TEST(OrderGatewayTest, DISABLED_OrderMassCancel) {
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

  TradeEngine* trade_engine_ = new TradeEngine(logger.get(), pool.get(),
                                               pool2.get(), temp);
  OrderGateway og(authorization, logger.get(), trade_engine_);

  RequestCommon request;
  std::string name_order = "Neworo_mass_cancel";// 계속 바꿔줘야됨

  request.req_type = ReqeustType::kOrderMassCancelRequest;
  request.order_name = name_order;
  request.symbol = "BTCUSDT";

  sleep(2);

  og.order_request(request);

  sleep(2);

  ResponseCommon response;
  response = trade_engine_->dequeue_response();

  ASSERT_EQ(response.res_type, ResponseType::kOrderMassCancelReport);
  EXPECT_EQ(response.order_mass_cancel_report->mass_cancel_response, MassCancelResponse::kCancelSymbolOrders);
  sleep(1);
}