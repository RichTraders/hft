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
#include "NewOroFix44_types.hpp"
#include "NewOroFix44_router.hpp"
#include "NewOroFix44_classes.hpp"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
using namespace core;


TEST(FixAppTest, CallbackRegistration) {
  auto app = FixApp("fix-md.testnet.binance.vision",
                    9000,
                    "BMDWATCH",
                    "SPOT");
  bool called = false;

  app.register_callback( //log on
      "A", [&](FIX8::Message* m) {
        called = true;
        std::string result;
        m->encode(result);
        std::cout <<result << std::endl;
      });
  app.register_callback( //log out
      "5", [&](FIX8::Message* m) {
        std::string result;
        m->encode(result);
        std::cout <<result << std::endl;
      });
  app.start();
  sleep(3);
  EXPECT_TRUE(called);

  app.stop();
}
