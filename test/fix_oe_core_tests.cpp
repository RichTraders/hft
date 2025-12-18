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


#include "core/fix/fix_oe_core.h"
#include "gtest/gtest.h"
#include <fix8/f8includes.hpp>
#include "core/fix/NewOroFix44/NewOroFix44OE_types.hpp"
#include "ini_config.hpp"
#include "memory_pool.hpp"
#include "logger.h"
#include "core/response_manager.h"
#include "order_entry.h"

using namespace core;
using namespace trading;
using namespace common;
using namespace FIX8::NewOroFix44OE;

std::string timestamp() {
  using std::chrono::duration_cast;
  using std::chrono::system_clock;
  using std::chrono::year_month_day;
  using std::chrono::days;

  using std::chrono::hours;
  using std::chrono::minutes;
  using std::chrono::seconds;
  using std::chrono::milliseconds;

  const auto now = system_clock::now();
  //FIX8 only supports ms
  const auto t = floor<milliseconds>(now);

  const auto dp = floor<days>(t);
  const auto ymd = year_month_day{dp};
  const auto time = t - dp;

  const auto h = duration_cast<hours>(time);
  const auto m = duration_cast<minutes>(time - h);
  const auto s = duration_cast<seconds>(time - h - m);
  const auto ms = duration_cast<milliseconds>(time - h - m - s);

  char buf[64];
  std::snprintf(buf, sizeof(buf), "%04d%02d%02d-%02d:%02d:%02d.%03ld",
                static_cast<int>(ymd.year()),
                static_cast<unsigned>(ymd.month()),
                static_cast<unsigned>(ymd.day()),
                static_cast<int>(h.count()), static_cast<int>(m.count()),
                static_cast<int>(s.count()),
                ms.count());
  return std::string(buf);
}

class FixTest : public ::testing::Test {
public:
  static std::unique_ptr<Logger> logger;
  static std::unique_ptr<Logger::Producer> producer;
  static std::unique_ptr<MemoryPool<trading::ExecutionReport>> execution_report_pool;
  static std::unique_ptr<MemoryPool<trading::OrderCancelReject>> order_cancel_reject_pool;
  static std::unique_ptr<MemoryPool<trading::OrderMassCancelReport>> order_mass_cancel_report_pool;
  static std::unique_ptr<ResponseManager> response_manager;
protected:
  void SetUp() override {
    INI_CONFIG.load("resources/config.ini");
    execution_report_pool = std::make_unique<MemoryPool<
          trading::ExecutionReport>>(1024);
    order_cancel_reject_pool = std::make_unique<MemoryPool<
      trading::OrderCancelReject>>(1024);
    order_mass_cancel_report_pool = std::make_unique<MemoryPool<
      trading::OrderMassCancelReport>>(1024);
    auto pool = std::make_unique<common::MemoryPool<OrderData>>(1024);
    logger = std::make_unique<Logger>();
    producer = std::make_unique<Logger::Producer>(logger->make_producer());
    response_manager = std::make_unique<ResponseManager>(
       *producer, execution_report_pool.get(), order_cancel_reject_pool.get(),
       order_mass_cancel_report_pool.get());

    fix = std::make_unique<FixOeCore>("SENDER", "TARGET", *producer, response_manager.get());
  }
  void TearDown() override {
    fix.reset();
  }

  std::unique_ptr<FixOeCore> fix;
};
std::unique_ptr<Logger> FixTest::logger;
std::unique_ptr<Logger::Producer> FixTest::producer;
std::unique_ptr<MemoryPool<trading::ExecutionReport>> FixTest::execution_report_pool;
std::unique_ptr<MemoryPool<trading::OrderCancelReject>> FixTest::order_cancel_reject_pool;
std::unique_ptr<MemoryPool<trading::OrderMassCancelReport>> FixTest::order_mass_cancel_report_pool;
std::unique_ptr<ResponseManager> FixTest::response_manager;

TEST_F(FixTest, CreateLogOnMessageProducesValidFixMessage) {
  std::string sig = timestamp();
  std::string timestamp = "20250101-01:01:12.123";

  std::string msg_str = fix->create_log_on_message(sig, timestamp);
  FIX8::Message* msg = fix->decode(msg_str);
  ASSERT_NE(msg, nullptr);

  EXPECT_EQ(msg->get_msgtype(), "A"); // Logon
  const SenderCompID* sender = msg->Header()->get<SenderCompID>();
  ASSERT_NE(sender, nullptr);
  EXPECT_EQ(sender->get(), "SENDER");

  const TargetCompID* target = msg->Header()->get<TargetCompID>();
  ASSERT_NE(target, nullptr);
  EXPECT_EQ(target->get(), "TARGET");

  const RawDataLength* raw_len = msg->get<RawDataLength>();
  ASSERT_NE(raw_len, nullptr);
  EXPECT_EQ(raw_len->get(), static_cast<int>(sig.size()));

  delete msg;
}

TEST_F(FixTest, CreateLogOutMessageProducesValidFixMessage) {
  std::string msg_str = fix->create_log_out_message();
  FIX8::Message* msg = fix->decode(msg_str);
  ASSERT_NE(msg, nullptr);

  EXPECT_EQ(msg->get_msgtype(), "5"); // Logout

  const SenderCompID* sender = msg->Header()->get<SenderCompID>();
  ASSERT_NE(sender, nullptr);
  EXPECT_EQ(sender->get(), "SENDER");

  const TargetCompID* target = msg->Header()->get<TargetCompID>();
  ASSERT_NE(target, nullptr);
  EXPECT_EQ(target->get(), "TARGET");

  const MsgSeqNum* seq = msg->Header()->get<MsgSeqNum>();
  ASSERT_NE(seq, nullptr);
  EXPECT_GT(seq->get(), 0);

  const SendingTime* time = msg->Header()->get<SendingTime>();
  ASSERT_NE(time, nullptr);

  delete msg;
}