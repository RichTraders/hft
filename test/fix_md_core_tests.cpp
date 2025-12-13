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


#include "core/fix/fix_md_core.h"
#include <fix8/f8includes.hpp>
#include "core/fix/NewOroFix44/NewOroFix44MD_types.hpp"
#include "core/fix/NewOroFix44/NewOroFix44MD_router.hpp"
#include "core/fix/NewOroFix44/NewOroFix44MD_classes.hpp"
#include "ini_config.hpp"

#include "gtest/gtest.h"
using namespace core;
using namespace FIX8::NewOroFix44MD;

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
protected:
  void SetUp() override {
   INI_CONFIG.load("resources/config.ini");
   pool_ = std::make_unique<common::MemoryPool<MarketData>>(1024);
   logger_ = std::make_unique<common::Logger>();
   fix = std::make_unique<FixMdCore>("SENDER", "TARGET", logger_.get(),
                                     pool_.get());
 }
  void TearDown() override {
    fix.reset();
  }

 std::unique_ptr<common::Logger> logger_;
 std::unique_ptr<FixMdCore> fix;
 std::unique_ptr<common::MemoryPool<MarketData>> pool_;
};

TEST_F(FixTest, CreateLogOnMessageProducesValidFixMessage) {
  const std::string sig = timestamp();
  const std::string timestamp = "20250101-01:01:12.123";

  const std::string msg_str = fix->create_log_on_message(sig, timestamp);
  FIX8::Message* msg = fix->decode(msg_str);
  ASSERT_NE(msg, nullptr);

  EXPECT_EQ(msg->get_msgtype(), "A"); // Logon
  const auto* sender = msg->Header()->get<SenderCompID>();
  ASSERT_NE(sender, nullptr);
  EXPECT_EQ(sender->get(), "SENDER");

  const auto* target = msg->Header()->get<TargetCompID>();
  ASSERT_NE(target, nullptr);
  EXPECT_EQ(target->get(), "TARGET");

  const auto* raw_len = msg->get<RawDataLength>();
  ASSERT_NE(raw_len, nullptr);
  EXPECT_EQ(raw_len->get(), static_cast<int>(sig.size()));

  delete msg;
}


TEST_F(FixTest, CreateLogOutMessageProducesValidFixMessage) {
  std::string msg_str = fix->create_log_out_message();
  FIX8::Message* msg = fix->decode(msg_str);
  ASSERT_NE(msg, nullptr);

  EXPECT_EQ(msg->get_msgtype(), "5"); // Logout

  const auto* sender = msg->Header()->get<SenderCompID>();
  ASSERT_NE(sender, nullptr);
  EXPECT_EQ(sender->get(), "SENDER");

  const auto* target = msg->Header()->get<TargetCompID>();
  ASSERT_NE(target, nullptr);
  EXPECT_EQ(target->get(), "TARGET");

  const MsgSeqNum* seq = msg->Header()->get<MsgSeqNum>();
  ASSERT_NE(seq, nullptr);
  EXPECT_GT(seq->get(), 0);

  const SendingTime* time = msg->Header()->get<SendingTime>();
  ASSERT_NE(time, nullptr);

  delete msg;
}

TEST_F(FixTest, CreateHeartbeatMessage_ContainsCorrectFields) {
  Heartbeat heartbeat;
  heartbeat << new TestReqID("111111");
  std::string const msg_str = fix->create_heartbeat_message(&heartbeat);
  FIX8::Message* msg = fix->decode(msg_str);
  ASSERT_NE(msg, nullptr);

  EXPECT_EQ(msg->get_msgtype(), "0"); // Heartbeat

  const auto* sender = msg->Header()->get<SenderCompID>();
  ASSERT_NE(sender, nullptr);
  EXPECT_EQ(sender->get(), "SENDER");

  const auto* target = msg->Header()->get<TargetCompID>();
  ASSERT_NE(target, nullptr);
  EXPECT_EQ(target->get(), "TARGET");

  const auto* seq = msg->Header()->get<MsgSeqNum>();
  ASSERT_NE(seq, nullptr);
  EXPECT_GT(seq->get(), 0);

  const auto* time = msg->Header()->get<SendingTime>();
  ASSERT_NE(time, nullptr);

  delete msg;
}

TEST_F(FixTest, CreateSubscriptionMessage_ContainsCorrectFields) {
  FixMdCore::RequestId req_id = "REQ-123";
  FixMdCore::MarketDepthLevel depth = "1";
  FixMdCore::SymbolId symbol = "BTCUSD";

  std::string const msg_str = fix->create_market_data_subscription_message(
      req_id, depth, symbol, true);
  FIX8::Message* msg = fix->decode(msg_str);
  ASSERT_NE(msg, nullptr);

  EXPECT_EQ(msg->get_msgtype(), "V"); // MarketDataRequest

  const auto* sender = msg->Header()->get<SenderCompID>();
  ASSERT_NE(sender, nullptr);
  EXPECT_EQ(sender->get(), "SENDER");

  const auto* target = msg->Header()->get<TargetCompID>();
  ASSERT_NE(target, nullptr);
  EXPECT_EQ(target->get(), "TARGET");

  const auto* reqid_field = msg->get<MDReqID>();
  ASSERT_NE(reqid_field, nullptr);
  EXPECT_EQ(reqid_field->get(), req_id);

  const auto* sub_type = msg->get<SubscriptionRequestType>();
  ASSERT_NE(sub_type, nullptr);
  EXPECT_EQ(sub_type->get(), '1');

  const auto* depth_field = msg->get<MarketDepth>();
  ASSERT_NE(depth_field, nullptr);
  EXPECT_EQ(depth_field->get(), std::stoi(depth));

  const auto* book = msg->get<AggregatedBook>();
  ASSERT_NE(book, nullptr);
  EXPECT_TRUE(book->get());

  delete msg;
}