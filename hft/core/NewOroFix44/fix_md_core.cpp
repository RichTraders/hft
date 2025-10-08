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
#include "authorization.h"

namespace core {
using namespace FIX8::NewOroFix44MD;
using namespace common;
constexpr int kEntries = 268;

FixMdCore::FixMdCore(SendId sender_comp_id, TargetId target_comp_id,
                     Logger* logger, MemoryPool<MarketData>* pool)
  : logger_(logger),
    sender_comp_id_(std::move(sender_comp_id)),
    target_comp_id_(std::move(target_comp_id)),
    market_data_pool_(pool) {
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
      << new RawData(sig_b64) << new Username(AUTHORIZATION.get_api_key())
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
    const RequestId& request_id, const MarketDepthLevel& level,
    const SymbolId& symbol, const bool subscribe) {
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
    mb->add_field(new MDEntryType('0'));  // Bid
    entry_types->add(mb);
    count++;

    mb = entry_types->create_group(true);
    mb->add_field(new MDEntryType('1'));  // Ask
    entry_types->add(mb);
    count++;

    mb = entry_types->create_group(true);
    mb->add_field(new MDEntryType('2'));  // Trade
    entry_types->add(mb);
    count++;

    request.add_field(new NoMDEntryTypes(count));
    request.add_group(entry_types);
  }

  {
    auto* entry_types = new MarketDataRequest::NoRelatedSym();
    FIX8::MessageBase* group = entry_types->create_group(true);
    group->add_field(new Symbol(symbol));  // Bid

    entry_types->add(group);
    request.add_field(new NoRelatedSym(1));
    request.add_group(entry_types);
  }

  request << new MDReqID(request_id)
          << new SubscriptionRequestType(subscribe ? '1': '2')
          << new MarketDepth(level)
  << new AggregatedBook(true);

  std::string wire;
  request.encode(wire);
  return wire;
}

//Does it really need?
std::string FixMdCore::create_trade_data_subscription_message(
    const RequestId& request_id, const MarketDepthLevel& level,
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
    message->add_field(new MDEntryType('2'));  // Bid
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

  request << new MDReqID(request_id) << new SubscriptionRequestType('1')

          << new MarketDepth(level) << new AggregatedBook(true);

  std::string wire;
  request.encode(wire);
  return wire;
}

std::string FixMdCore::create_instrument_list_request_message(
    const std::string& symbol) {
  FIX8::NewOroFix44MD::InstrumentListRequest request(false);
  request.Header()->add_field(new SenderCompID(sender_comp_id_));
  request.Header()->add_field(new TargetCompID(target_comp_id_));
  request.Header()->add_field(new MsgSeqNum(sequence_++));
  request.Header()->add_field(new SendingTime());
  if (symbol.empty()) {
    request << new InstrumentReqID("BTCUSDT")
            << new InstrumentListRequestType(4);
  }else {
    request << new InstrumentReqID("BTCUSDT")
            << new InstrumentListRequestType(0)
            << new Symbol(symbol);
  }

  std::string wire;
  request.encode(wire);
  return wire;
}

MarketUpdateData FixMdCore::create_market_data_message(FIX8::Message* msg) {
  auto* entries = msg->find_group(kEntries);
  const std::vector<MarketData*> data(entries->size());

  //Check first entry to confirm whether trade event or market data
  const FIX8::MessageBase* entry = entries->get_element(0);
  if (unlikely(!entry))
    return MarketUpdateData(kNone, std::move(data));

  if (entry->get<TradeID>()) {
    return _create_trade_data_message(entries);
  }
  return _create_market_data_message(entries);
}

MarketUpdateData FixMdCore::_create_market_data_message(
    const FIX8::GroupBase* entries) const {
  std::vector<MarketData*> data;
  data.reserve(entries->size());
  const auto* symbol = entries->get_element(0)->get<Symbol>();  //55

  const FIX8::MessageBase* entry = entries->get_element(0);
  if (unlikely(!entry))
    return MarketUpdateData(kMarket, std::move(data));

  const auto* first_book_update_id =  entry->get<FirstBookUpdateID>();
  const auto* last_book_update_id = entry->get<LastBookUpdateID>();

  if (unlikely(first_book_update_id == nullptr || last_book_update_id == nullptr)) {
    return MarketUpdateData(kMarket, std::move(data));
  }

  const auto* first_side = entry->get<MDEntryType>();
  const auto* first_price = entry->get<MDEntryPx>();
  const auto* first_qty = entry->get<MDEntrySize>();
  const auto* first_action = entry->get<MDUpdateAction>();

  data.push_back(
    market_data_pool_->allocate(
      charToMarketUpdateType( first_action->get()),
      OrderId{kOrderIdInvalid},
      symbol->get(),
      first_side->get(),
      Price{static_cast<float>(first_price->get())},
      first_qty == nullptr
        ? Qty{kQtyInvalid}
        : Qty{static_cast<float>(first_qty->get())}));

  for (size_t i = 1; i < entries->size(); ++i) {
    const FIX8::MessageBase* entry = entries->get_element(i);
    if (unlikely(!entry))
      continue;
    const auto* side = entry->get<MDEntryType>();       // 269
    const auto* price = entry->get<MDEntryPx>();       // 270
    const auto* qty = entry->get<MDEntrySize>();       // 271
    const auto* action = entry->get<MDUpdateAction>();  // 279
    auto market_data = market_data_pool_->allocate(
        charToMarketUpdateType(action->get()),
            OrderId{kOrderIdInvalid},
            symbol->get(),
            side->get(),
            Price{static_cast<float>(price->get())},
            qty == nullptr
              ? Qty{kQtyInvalid}
              : Qty{static_cast<float>(qty->get())});
    while (market_data == nullptr) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      market_data = market_data_pool_->allocate(
        charToMarketUpdateType(action->get()),
            OrderId{kOrderIdInvalid},
            symbol->get(),
            side->get(),
            Price{static_cast<float>(price->get())},
            qty == nullptr
              ? Qty{kQtyInvalid}
              : Qty{static_cast<float>(qty->get())});
    }
    data.push_back(market_data);
  }
  return MarketUpdateData(
    std::stoull(first_book_update_id->get()),
    std::stoull(last_book_update_id->get()),
    kMarket,
    std::move(data));
}

MarketUpdateData FixMdCore::_create_trade_data_message(
    const FIX8::GroupBase* entries) const {
  std::vector<MarketData*> data;
  data.reserve(entries->size());
  const auto* symbol = entries->get_element(0)->get<Symbol>();  //55

  //Check first entry to confirm whether trade event or market data
  const FIX8::MessageBase* entry = entries->get_element(0);
  if (unlikely(!entry))
    return MarketUpdateData(kTrade, std::move(data));

  const auto* first_side = entry->get<MDEntryType>();
  const auto* first_price = entry->get<MDEntryPx>();
  const auto* first_qty = entry->get<MDEntrySize>();

  data.push_back(
   market_data_pool_->allocate(
     MarketUpdateType::kTrade,
     OrderId{kOrderIdInvalid},
     symbol->get(),
     first_side->get(),
     Price{static_cast<float>(first_price->get())},
     first_qty == nullptr
       ? Qty{kQtyInvalid}
       : Qty{static_cast<float>(first_qty->get())}));

  for (size_t i = 1; i < entries->size(); ++i) {
    const FIX8::MessageBase* entry = entries->get_element(i);
    if (unlikely(!entry))
      continue;
    const auto* side = entry->get<MDEntryType>();       // 269
    const auto* price = entry->get<MDEntryPx>();       // 270
    const auto* qty = entry->get<MDEntrySize>();       // 271
    data.push_back(
        market_data_pool_->allocate(
          MarketUpdateType::kTrade,
          OrderId{kOrderIdInvalid},
          symbol->get(),
          side->get(),
          Price{static_cast<float>(price->get())},
          qty == nullptr
            ? Qty{kQtyInvalid}
            : Qty{static_cast<float>(qty->get())}));
  }
  return MarketUpdateData(kTrade, std::move(data));
}

MarketUpdateData FixMdCore::create_snapshot_data_message(FIX8::Message* msg) {
  const auto* symbol = msg->get<Symbol>();
  auto* entries = msg->find_group(kEntries);

  std::vector<MarketData*> data;
  data.reserve(entries->size() + 1);

  data.push_back(
      market_data_pool_->allocate(
          MarketUpdateType::kClear,
          OrderId{},
          TickerId{symbol->get()},
          Side::kInvalid,
          Price{},
          Qty{}));
  const auto& last_book_update_id = msg->get<LastBookUpdateID>()->get();

  for (size_t i = 0; i < entries->size(); ++i) {
    const FIX8::MessageBase* entry = entries->get_element(i);
    if (unlikely(!entry))
      continue;
    const auto* side = entry->get<MDEntryType>();   // 269
    const auto* price = entry->get<MDEntryPx>();   // 270
    const auto* qty = entry->get<MDEntrySize>();   // 271
    data.push_back(
        market_data_pool_->allocate(
            MarketUpdateType::kAdd,
            OrderId{kOrderIdInvalid},
            TickerId{symbol->get()},
            charToSide(side->get()),
            Price{static_cast<float>(price->get())},
            Qty{static_cast<float>(qty->get())}));
  }
  return MarketUpdateData(
    0ULL,
    std::stoull(last_book_update_id),
    kMarket,
    std::move(data));
}

InstrumentInfo FixMdCore::create_instrument_list_message(FIX8::Message* msg) {
  InstrumentInfo out{};
  if (!msg) return out;

  if (const auto* f = msg->get<InstrumentReqID>()) {
    out.instrument_req_id = f->get();
  }

  const FIX8::GroupBase* group = msg->find_group(kNoRelatedSym);
  if (group) {
    out.symbols.reserve(group->size());
    for (size_t i = 0; i < group->size(); ++i) {
      const FIX8::MessageBase* g = group->get_element(i);
      if (!g) continue;

      InstrumentInfo::RelatedSymT related_symbol{};

      if (const auto* field = g->get<Symbol>())                  related_symbol.symbol = field->get();            // 55
      if (const auto* field = g->get<Currency>())                related_symbol.currency = field->get();          // 15
      if (const auto* field = g->get<MinTradeVol>())             related_symbol.min_trade_vol = field->get();     // 562
      if (const auto* field = g->get<MaxTradeVol>())             related_symbol.max_trade_vol = field->get();     // 1140
      if (const auto* field = g->get<MinQtyIncrement>())         related_symbol.min_qty_increment = field->get(); // 25039
      if (const auto* field = g->get<MarketMinTradeVol>())       related_symbol.market_min_trade_vol = field->get();   // 25040
      if (const auto* field = g->get<MarketMaxTradeVol>())       related_symbol.market_max_trade_vol = field->get();   // 25041
      if (const auto* field = g->get<MarketMinQtyIncrement>())   related_symbol.market_min_qty_increment = field->get(); // 25042
      if (const auto* field = g->get<MinPriceIncrement>())       related_symbol.min_price_increment = field->get();     // 969

      out.symbols.emplace_back(std::move(related_symbol));
    }
  }

  return out;
}

MarketDataReject FixMdCore::create_reject_message(FIX8::Message* msg) {
  const auto ref_sequence = msg->get<RefSeqNum>();
  const auto msg_type = msg->get<RefMsgType>();
  const auto rejected_type = msg->get<SessionRejectReason>();
  const auto error_message = msg->get<Text>();
  const auto error_code = msg->get<ErrorCode>();

  if (ref_sequence != nullptr)
    logger_->info(std::format("failed sequence :{}", ref_sequence->get()));

  return MarketDataReject{
      .session_reject_reason =
          msg_type != nullptr ? msg_type->get() : "NO REASON",
      .rejected_message_type =
          rejected_type != nullptr ? rejected_type->get() : -1,
      .error_message =
          error_message != nullptr ? error_message->get() : "NO ERROR MESSAGE",
      .error_code = error_code != nullptr ? error_code->get() : -1};
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

}  // namespace core