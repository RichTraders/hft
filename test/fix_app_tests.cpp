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

#include "NewOroFix44/fix_md_app.h"
#include "NewOroFix44/fix_oe_app.h"
#include <fix8/f8includes.hpp>
#include "NewOroFix44MD_types.hpp"
#include "NewOroFix44MD_router.hpp"
#include "NewOroFix44MD_classes.hpp"
#include "NewOroFix44OE_types.hpp"
#include "NewOroFix44OE_router.hpp"
#include "NewOroFix44OE_classes.hpp"

#include "logger.h"
#include "memory_pool.hpp"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
using namespace core;
using namespace common;

TEST(FixAppTest, DISABLED_CallbackRegistration) {
  auto pool = std::make_unique<MemoryPool<MarketData>>(1024);
  auto logger = std::make_unique<Logger>();
  auto app = FixMarketDataApp("fix-md.testnet.binance.vision",
                              9000,
                              "BMDWATCH",
                              "SPOT", logger.get(), pool.get());
  bool login_success = false;
  bool logout_success = false;

  app.register_callback( //log on
      "A", [&](FIX8::Message* m) {
        login_success = true;
        std::string result;
        m->encode(result);
        std::cout << result << std::endl;
      });
  app.register_callback( //log out
      "5", [&](FIX8::Message* m) {
        std::string result;
        m->encode(result);
        logout_success = true;
        std::cout << result << std::endl;
      });
  app.start();
  sleep(5);
  EXPECT_TRUE(login_success);

  app.stop();
  sleep(5);
  EXPECT_TRUE(logout_success);
}

TEST(FixAppTest, CallbackFixOERegistration) {
  auto pool = std::make_unique<MemoryPool<OrderData>>(1024);
  auto logger = std::make_unique<Logger>();
  auto app = FixOrderEntryApp("fix-oe.testnet.binance.vision",
                              9000,
                              "BMDWATCH",
                              "SPOT",
                              logger.get(),
                              pool.get());
  bool login_called = false;
  bool heartbeat_called = false;
  bool excution_report_called = false;

  app.register_callback( //log on
      "A", [&](FIX8::Message* m) {
        login_called = true;
        std::string result;
        m->encode(result);
        std::cout << result << std::endl;
      });
  app.register_callback( //log out
      "5", [&](FIX8::Message* m) {
        std::string result;
        m->encode(result);
        std::cout << result << std::endl;
      });
  app.register_callback(
      "1", [&](FIX8::Message* m) {
        auto message = app.create_heartbeat(m);
        app.send(message);
        heartbeat_called = true;
      });
  app.register_callback("8", [&](FIX8::Message* m) {
    // 원형이 MessageBase* 이므로, 구체 타입으로 캐스트
    auto* exec = static_cast<FIX8::NewOroFix44OE::ExecutionReport*>(m);
    // 문자열 포맷 함수 호출
    trading::ExecutionReport ret = app.create_execution_report_message(exec);
    // 로그 출력 혹은 화면에 표시
    excution_report_called = true;
  });

  app.start();
  sleep(3);
  EXPECT_TRUE(login_called);

  trading::NewSingleOrderData order_data;
  order_data.cl_order_id = "Neworo";
  order_data.symbol = "BTCUSDT";
  order_data.side = trading::Side::kBuy;
  order_data.order_qty = 0.01;
  order_data.price = 117984;
  order_data.transactTime = app.timestamp();
  order_data.ord_type = trading::OrderType::kMarket;
  order_data.time_in_force = trading::TimeInForce::kGoodTillCancel;
  order_data.self_trade_prevention_mode = trading::SelfTradePreventionMode::kExpire_Taker;

  std::string ret = app.create_order_message(order_data);
  app.send(ret);

  sleep(100);
  EXPECT_TRUE(heartbeat_called);
}