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

int cl_order_id = 2065;

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

  auto execution_report_pool = std::make_unique<MemoryPool<
      ExecutionReport>>(1024);
  auto order_cancel_reject_pool = std::make_unique<MemoryPool<
    OrderCancelReject>>(1024);
  auto order_mass_cancel_report_pool = std::make_unique<MemoryPool<
    OrderMassCancelReport>>(1024);

  ResponseManager* response_manager = new ResponseManager(
      logger.get(), execution_report_pool.get(), order_cancel_reject_pool.get(),
      order_mass_cancel_report_pool.get());

  TradeEngine* trade_engine_ = new TradeEngine(logger.get(), pool.get(),
                                               pool2.get(), response_manager, temp);
  OrderGateway og(authorization, logger.get(), trade_engine_, response_manager);

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

  sleep(5);

  og.order_request(request);

  sleep(100);
}


TEST(OrderGatewayTest, DISABLED_OrderCancel) {
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

  ResponseManager* response_manager = new ResponseManager(
      logger.get(), execution_report_pool.get(), order_cancel_reject_pool.get(),
      order_mass_cancel_report_pool.get());

  TradeEngine* trade_engine_ = new TradeEngine(logger.get(), pool.get(),
                                               pool2.get(), response_manager, temp);
  OrderGateway og(authorization, logger.get(), trade_engine_, response_manager);

  RequestCommon request;

  request.req_type = ReqeustType::kOrderCancelRequest;
  request.cl_order_id.value = cl_order_id + 1;
  request.orig_cl_order_id.value = cl_order_id;
  request.symbol = "BTCUSDT";

  sleep(2);

  og.order_request(request);

  sleep(100);
}

TEST(OrderGatewayTest, OrderMassCancel) {
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

  ResponseManager* response_manager = new ResponseManager(
      logger.get(), execution_report_pool.get(), order_cancel_reject_pool.get(),
      order_mass_cancel_report_pool.get());

  TradeEngine* trade_engine_ = new TradeEngine(logger.get(), pool.get(),
                                               pool2.get(), response_manager, temp);
  OrderGateway og(authorization, logger.get(), trade_engine_, response_manager);

  RequestCommon request;

  request.req_type = ReqeustType::kOrderMassCancelRequest;
  request.cl_order_id.value = cl_order_id;
  request.symbol = "BTCUSDT";

  sleep(2);

  og.order_request(request);

  sleep(100);
}