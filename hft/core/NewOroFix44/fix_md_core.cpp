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

#include <fix8/f8includes.hpp>
#include "NewOroFix44MD_types.hpp"
#include "NewOroFix44MD_router.hpp"
#include "NewOroFix44MD_classes.hpp"
#include "performance.h"

namespace core {
using namespace FIX8::NewOroFix44MD;
using namespace common;
constexpr int kMarketDataPoolSize = 2048;
constexpr int kEntries = 268;

FixMdCore::FixMdCore(SendId sender_comp_id, TargetId target_comp_id,
                     Logger* logger, MemoryPool<MarketData>* pool,
                     const Authorization& authorization)
  : logger_(logger),
    sender_comp_id_(std::move(sender_comp_id)),
    target_comp_id_(std::move(target_comp_id)),
    market_data_pool_(pool),
    authorization_(authorization) {
  logger_->info("[Constructor] FixMdCore Created");
}

FixMdCore::~FixMdCore() {
  logger_->info("[Destructor] FixMdCore Destroy");
}

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
      << new RawData(sig_b64) << new Username(authorization_.api_key)
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
std::string FixMdCore::create_trade_data_subscription_message(
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

MarketUpdateData FixMdCore::create_market_data_message(FIX8::Message* msg) {
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

MarketUpdateData FixMdCore::create_snapshot_data_message(FIX8::Message* msg) {
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

FIX8::Message* FixMdCore::decode(const std::string& message) {
  START_MEASURE(Convert_Message);
  FIX8::Message* msg(FIX8::Message::factory(ctx(), message, true, true));
  END_MEASURE(Convert_Message, logger_);
  if (LIKELY(msg)) {
    return msg;
  }
  return nullptr;
}

}