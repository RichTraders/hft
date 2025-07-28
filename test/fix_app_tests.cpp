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

#include "NewOroFix44/fix_app.h"
#include <fix8/f8includes.hpp>
#include "NewOroFix44MD_types.hpp"
#include "NewOroFix44MD_router.hpp"
#include "NewOroFix44MD_classes.hpp"

#include "logger.h"
#include "memory_pool.hpp"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
using namespace core;
using namespace common;

TEST(FixAppTest, CallbackRegistration) {
  auto pool = std::make_unique<MemoryPool<MarketData>>(1024);
  auto logger = std::make_unique<Logger>();
  auto app = FixApp("fix-md.testnet.binance.vision",
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
  sleep(3);
  EXPECT_TRUE(login_success);

  app.stop();
  sleep(3);
  EXPECT_TRUE(logout_success);
}