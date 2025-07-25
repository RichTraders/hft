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
#include "signature.h"

#include <fix8/f8includes.hpp>
#include "NewOroFix44_types.hpp"
#include "NewOroFix44_router.hpp"
#include "NewOroFix44_classes.hpp"
#include "performance.h"

namespace core {
using namespace FIX8::NewOroFix44;
using namespace common;
constexpr int kMarketDataPoolSize = 2048;
constexpr int kEntries = 268;

Fix::Fix(SendId sender_comp_id,
         TargetId target_comp_id,
         Logger* logger,
         MemoryPool<MarketData>* pool)
  : logger_(logger),
    sender_comp_id_(std::move(sender_comp_id)),
    target_comp_id_(std::move(target_comp_id)),
    market_data_pool_(pool) {}

std::string Fix::create_log_on_message(const std::string& sig_b64,
                                       const std::string& timestamp) {
  FIX8::NewOroFix44_ctx();
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
#ifdef DEBUG
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

std::string Fix::create_log_out_message() {
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

std::string Fix::create_heartbeat_message(FIX8::Message* message) {
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

std::string Fix::create_market_data_subscription_message(
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
    int count = 0;
    auto* entry_types = new MarketDataRequest::NoMDEntryTypes();
    FIX8::MessageBase* mb = entry_types->create_group(true);
    mb->add_field(new MDEntryType('0')); // Bid
    entry_types->add(mb);
    count++;

    mb = entry_types->create_group(true);
    mb->add_field(new MDEntryType('1')); // Ask
    entry_types->add(mb);
    count++;

    mb = entry_types->create_group(true);
    mb->add_field(new MDEntryType('2')); // Trade
    entry_types->add(mb);
    count++;

    request.add_field(new NoMDEntryTypes(count));
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

//Does it really need?
std::string Fix::create_trade_data_subscription_message(
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
    FIX8::MessageBase* message = entry_types->create_group(true);
    message->add_field(new MDEntryType('2')); // Bid
    entry_types->add(message);

    request.add_field(new NoMDEntryTypes(1));
    request.add_group(entry_types);
  }

  {
    auto* entry_types = new MarketDataRequest::NoRelatedSym();
    FIX8::MessageBase* group = entry_types->create_group(true);
    group->add_field(new Symbol(symbol));

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

MarketUpdateData Fix::create_market_data(FIX8::Message* msg) const {
  auto* entries = msg->find_group(kEntries);
  std::vector<MarketData*> data(entries->size());
  const auto* symbol = entries->get_element(0)->get<Symbol>(); //55

  for (size_t i = 0; i < entries->size(); ++i) {
    const FIX8::MessageBase* entry = entries->get_element(i);
    if (unlikely(!entry))
      continue;
    const auto* side = entry->get<MDEntryType>(); // 269
    const auto* price = entry->get<MDEntryPx>(); // 270
    const auto* qty = entry->get<MDEntrySize>(); // 271
    const auto* action = entry->get<MDUpdateAction>(); //279
    data[i] =
        market_data_pool_->allocate(
            (charToSide(side->get()) == common::Side::kTrade)
              ? MarketUpdateType::kTrade
              : charToMarketUpdateType(action->get()),
            OrderId{kOrderIdInvalid},
            symbol->get(),
            side->get(),
            common::Price{static_cast<float>(price->get())},
            qty == nullptr
              ? Qty{kQtyInvalid}
              : Qty{static_cast<float>(qty->get())});
  }
  return MarketUpdateData(std::move(data));
}

MarketUpdateData Fix::create_snapshot_data_message(FIX8::Message* msg) const {
  const auto* symbol = msg->get<Symbol>();
  auto* entries = msg->find_group(kEntries);

  std::vector<MarketData*> data(entries->size() + 1);
  int index = 0;
  data[index++] =
      market_data_pool_->allocate(
          MarketUpdateType::kClear,
          OrderId{kOrderIdInvalid},
          TickerId{symbol->get()},
          common::Side::kInvalid,
          common::Price{kPriceInvalid},
          Qty{kQtyInvalid});

  for (size_t i = 0; i < entries->size(); ++i) {
    const FIX8::MessageBase* entry = entries->get_element(i);
    if (unlikely(!entry))
      continue;
    const auto* side = entry->get<MDEntryType>(); // 269
    const auto* price = entry->get<MDEntryPx>(); // 270
    const auto* qty = entry->get<MDEntrySize>(); // 271
    data[index++] =
        market_data_pool_->allocate(
            MarketUpdateType::kAdd,
            OrderId{kOrderIdInvalid},
            TickerId{symbol->get()},
            common::Side{side->get()},
            common::Price{static_cast<float>(price->get())},
            Qty{static_cast<float>(qty->get())});
  }
  return MarketUpdateData(std::move(data));
}

std::string Fix::timestamp() {
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

FIX8::Message* Fix::decode(const std::string& message) {
#ifdef DEBUG
  START_MEASURE(Convert_Message);
#endif
  FIX8::Message* msg(
      FIX8::Message::factory(ctx(), message, true, true));
#ifdef DEBUG
  END_MEASURE(Convert_Message, logger_);
#endif
  if (likely(msg)) {
    return msg;
  }
  return nullptr;
}

const std::string
Fix::get_signature_base64(const std::string& timestamp) const {
  // TODO(jb): use config reader
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

void Fix::encode(std::string& data, FIX8::Message* msg) {
  auto* ptr = data.data();
  msg->encode(&ptr);
}
}