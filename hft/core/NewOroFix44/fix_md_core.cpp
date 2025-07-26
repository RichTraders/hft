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

#include "fix_md_core.h"
#include "signature.h"

#include <fix8/f8includes.hpp>
#include "NewOroFix44MD_types.hpp"
#include "NewOroFix44MD_router.hpp"
#include "NewOroFix44MD_classes.hpp"
#include "performance.h"

namespace core {
using namespace FIX8::NewOroFix44MD;

FixMdCore::FixMdCore(SendId sender_comp_id, TargetId target_comp_id)
  : sender_comp_id_(std::move(sender_comp_id)),
    target_comp_id_(std::move(target_comp_id)) {}

std::string FixMdCore::create_log_on_message(const std::string& sig_b64,
                                       const std::string& timestamp) {
  FIX8::NewOroFix44MD_ctx();
  Logon request;

  FIX8::MessageBase* header = request.Header();
  *header
      << new SenderCompID(sender_comp_id_)
      << new TargetCompID(target_comp_id_)
      << new MsgSeqNum(sequence_++)
      << new SendingTime(timestamp);

  request << new EncryptMethod(EncryptMethod_NONE)
      << new HeartBtInt(30)
      << new ResetSeqNumFlag(true)
      << new RawDataLength(static_cast<int>(sig_b64.size()))
      << new RawData(sig_b64)
      << new Username(
#ifdef UNIT_TEST
          "XMJMVrlohHOtkzAn6WyiRQngkEqiSgJwacbMX3J5k0YwJx8Y7S0jE9xUsvwNclO9")
#else
          "cJHjHNqHUG1nhTs0YPEKlmxoXokNomptrrilcGzrhoqhd8S9kEFfcJg2YQjVKgGw")
#endif
      << new MessageHandling(2);

  if (auto* scid = static_cast<MsgType*>(request.Header()->get_field(35)))
    scid->set("A");

  std::string wire;
  request.encode(wire);
  return wire;
}

std::string FixMdCore::create_log_out_message() {
  Logout request;
  request.Header()->add_field(new SenderCompID(sender_comp_id_));
  request.Header()->add_field(new TargetCompID(target_comp_id_));
  request.Header()->add_field(new MsgSeqNum(sequence_++));
  request.Header()->add_field(new SendingTime());
  if (auto* scid = static_cast<MsgType*>(request.Header()->get_field(35)))
    scid->set("5");

  std::string wire;
  request.encode(wire);
  return wire;
}

std::string FixMdCore::create_heartbeat_message(FIX8::Message* message) {
  auto test_req_id = message->get<TestReqID>();

  Heartbeat request;
  request.Header()->add_field(new SenderCompID(sender_comp_id_));
  request.Header()->add_field(new TargetCompID(target_comp_id_));
  request.Header()->add_field(new MsgSeqNum(sequence_++));
  request.Header()->add_field(new SendingTime());
  request << new TestReqID(*test_req_id);
  if (auto* scid = static_cast<MsgType*>(request.Header()->get_field(35)))
    scid->set("0");

  std::string wire;
  request.encode(wire);
  return wire;
}

std::string FixMdCore::create_market_data_subscription_message(
    const RequestId& request_id,
    const MarketDepthLevel& level,
    const SymbolId& symbol) {
  MarketDataRequest request(false);
  request.Header()->add_field(new SenderCompID(sender_comp_id_));
  request.Header()->add_field(new TargetCompID(target_comp_id_));
  request.Header()->add_field(new MsgSeqNum(sequence_++));
  request.Header()->add_field(new SendingTime());

  if (auto* scid = static_cast<MsgType*>(request.Header()->get_field(35)))
    scid->set("V");

  {
    auto* entry_types = new MarketDataRequest::NoMDEntryTypes();
    FIX8::MessageBase* mb = entry_types->create_group(true);
    mb->add_field(new MDEntryType('0')); // Bid
    entry_types->add(mb);

    mb = entry_types->create_group(true);
    mb->add_field(new MDEntryType('1')); // Ask
    entry_types->add(mb);

    request.add_field(new NoMDEntryTypes(2));
    request.add_group(entry_types);
  }

  {
    auto* entry_types = new MarketDataRequest::NoRelatedSym();
    FIX8::MessageBase* group = entry_types->create_group(true);
    group->add_field(new Symbol(symbol)); // Bid

    entry_types->add(group);
    request.add_field(new NoRelatedSym(1));
    request.add_group(entry_types);
  }

  request << new MDReqID(request_id)
      << new SubscriptionRequestType('1')

      << new MarketDepth(level)
      << new AggregatedBook(true);

  std::string wire;
  request.encode(wire);
  return wire;
}

std::string FixMdCore::timestamp() {
  using namespace std::chrono;

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
                static_cast<unsigned>(ymd.month()), unsigned(ymd.day()),
                static_cast<int>(h.count()), int(m.count()), int(s.count()),
                long(ms.count()));
  return std::string(buf);
}

FIX8::Message* FixMdCore::decode(const std::string& message) {
#ifdef DEBUG
  START_MEASURE(Convert_Message);
#endif
  FIX8::Message* msg(
      FIX8::Message::factory(ctx(), message, true, true));
#ifdef DEBUG
  END_MEASURE(Convert_Message);
#endif
  if (likely(msg)) {
    return msg;
  }
  return nullptr;
}

const std::string FixMdCore::get_signature_base64(const std::string& timestamp) {
  EVP_PKEY* private_key = Util::load_ed25519(
      "/home/neworo/CLionProjects/hft/resources/private.pem", "akaj124!");

  // payload = "A<SOH>Sender<SOH>Target<SOH>1<SOH>20250709-00:49:41.041346"
  const std::string payload = std::string("A") + SOH
                              + sender_comp_id_ + SOH
                              + target_comp_id_ + SOH
                              + "1" + SOH
                              + timestamp;

  return Util::sign_and_base64(private_key, payload);
}

void FixMdCore::encode(std::string& data, FIX8::Message* msg) {
  auto* ptr = data.data();
  msg->encode(&ptr);
}
}