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

#include "core/fix/fix_md_app.h"
#include <fix8/f8includes.hpp>

#include "ini_config.hpp"
#include "logger.h"
#include "memory_pool.hpp"

#include "gmock/gmock.h"
#include "gtest/gtest.h"


class FixMdAppTest : public ::testing::Test  {
protected:
  static void SetUpTestSuite() {
    INI_CONFIG.load("resources/config.ini");

    market_data_pool_ = std::make_unique<common::MemoryPool<MarketData>>(1024);
    logger_ = std::make_unique<common::Logger>();
    producer_ = std::make_unique<common::Logger::Producer>(logger_->make_producer());
    app_ = std::make_unique<core::FixMarketDataApp>("BMDWATCH",
                                "SPOT",
                                *producer_,
                                market_data_pool_.get());
  }
  static void TearDownTestSuite() {
    app_.reset();
    std::cout << "TearDown OrderGatewayTest" << std::endl;
  }

public:
  static std::unique_ptr<common::MemoryPool<
          MarketData>> market_data_pool_;
  static std::unique_ptr<core::FixMarketDataApp> app_;
  static std::unique_ptr<common::Logger> logger_;
  static std::unique_ptr<common::Logger::Producer> producer_;

};

TEST_F(FixMdAppTest, DISABLED_CallbackRegistration) {
  std::mutex mtx;
  std::condition_variable cv;
  bool logout_success = false;
  bool login_success = false;


  app_->register_callback( //log on
      "A", [&](FIX8::Message* m) {
        login_success = true;
        std::string result;
        m->encode(result);
        std::cout << result << std::endl;
      });
  app_->register_callback( //log out
      "5", [&](FIX8::Message* m) {
        std::string result;
        std::lock_guard<std::mutex> lock(mtx);
        logout_success = true;
        m->encode(result);
        cv.notify_one();
        std::cout << result << std::endl;
      });

  app_->start();

  sleep(4);
  EXPECT_TRUE(login_success);

  app_->stop();

  {
    std::unique_lock<std::mutex> lock(mtx);
    // logout_success == true 가 될 때까지 대기
    cv.wait(lock, [&] { return logout_success; });
  }

}

std::unique_ptr<common::MemoryPool<
          MarketData>> FixMdAppTest::market_data_pool_;

std::unique_ptr<core::FixMarketDataApp> FixMdAppTest::app_;
std::unique_ptr<common::Logger> FixMdAppTest::logger_;
std::unique_ptr<common::Logger::Producer> FixMdAppTest::producer_;
