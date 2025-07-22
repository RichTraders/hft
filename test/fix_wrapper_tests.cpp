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


#include "fix_wrapper.h"
#include <fix8/f8includes.hpp>
#include "NewOroFix44_types.hpp"
#include "NewOroFix44_router.hpp"
#include "NewOroFix44_classes.hpp"

#include "gtest/gtest.h"
using namespace core;
using namespace FIX8::NewOroFix44;

class FixTest : public ::testing::Test {
protected:
  void SetUp() override {
    fix = std::make_unique<Fix>("SENDER", "TARGET");
  }

  std::unique_ptr<Fix> fix;
};

TEST_F(FixTest, CreateLogOnMessageProducesValidFixMessage) {
  std::string sig = fix->timestamp();
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

TEST_F(FixTest, CreateHeartbeatMessage_ContainsCorrectFields) {
  Heartbeat heartbeat;
  heartbeat << new TestReqID("111111");
  std::string msg_str = fix->create_heartbeat_message(&heartbeat);
  FIX8::Message* msg = fix->decode(msg_str);
  ASSERT_NE(msg, nullptr);

  EXPECT_EQ(msg->get_msgtype(), "0"); // Heartbeat

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

TEST_F(FixTest, CreateSubscriptionMessage_ContainsCorrectFields) {
  Fix::RequestId req_id = "REQ-123";
  Fix::MarketDepthLevel depth = "1";
  Fix::SymbolId symbol = "BTCUSD";

  std::string msg_str = fix->create_market_data_subscription_message(
      req_id, depth, symbol);
  FIX8::Message* msg = fix->decode(msg_str);
  ASSERT_NE(msg, nullptr);

  EXPECT_EQ(msg->get_msgtype(), "V"); // MarketDataRequest

  const SenderCompID* sender = msg->Header()->get<SenderCompID>();
  ASSERT_NE(sender, nullptr);
  EXPECT_EQ(sender->get(), "SENDER");

  const TargetCompID* target = msg->Header()->get<TargetCompID>();
  ASSERT_NE(target, nullptr);
  EXPECT_EQ(target->get(), "TARGET");

  const MDReqID* reqid_field = msg->get<MDReqID>();
  ASSERT_NE(reqid_field, nullptr);
  EXPECT_EQ(reqid_field->get(), req_id);

  const SubscriptionRequestType* sub_type = msg->get<SubscriptionRequestType>();
  ASSERT_NE(sub_type, nullptr);
  EXPECT_EQ(sub_type->get(), '1');

  const MarketDepth* depth_field = msg->get<MarketDepth>();
  ASSERT_NE(depth_field, nullptr);
  EXPECT_EQ(depth_field->get(), std::stoi(depth));

  const AggregatedBook* book = msg->get<AggregatedBook>();
  ASSERT_NE(book, nullptr);
  EXPECT_TRUE(book->get());

  delete msg;
}