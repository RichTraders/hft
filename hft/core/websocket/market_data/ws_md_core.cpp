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

#include "ws_md_core.h"

#include "schema/request/exchange_info.h"

namespace core {

WsMdCore::WsMdCore(common::Logger* logger, common::MemoryPool<MarketData>* pool)
    : logger_(logger->make_producer()),
      decoder_(logger_),
      mapper_(logger_, pool),
      encoder_(logger_),
      market_data_pool_(pool) {}

std::string WsMdCore::create_market_data_subscription_message(
    const RequestId& request_id, const MarketDepthLevel& level,
    const SymbolId& symbol, bool subscribe) const {
  return encoder_.create_market_data_subscription_message(request_id,
      level,
      symbol,
      subscribe);
}

std::string WsMdCore::create_trade_data_subscription_message(
    const RequestId& request_id, const MarketDepthLevel& level,
    const SymbolId& symbol) const {
  return encoder_.create_trade_data_subscription_message(request_id,
      level,
      symbol);
}

std::string WsMdCore::request_instrument_list_message(
    const std::string& symbol) const {
  return encoder_.request_instrument_list_message(symbol);
}

MarketUpdateData WsMdCore::create_market_data_message(
    const WireMessage& msg) const {
  return mapper_.to_market_data(msg);
}

MarketUpdateData WsMdCore::create_snapshot_data_message(
    const WireMessage& msg) const {
  return mapper_.to_snapshot_data(msg);
}

InstrumentInfo WsMdCore::create_instrument_list_message(
    const WireMessage& msg) const {
  return mapper_.to_instrument_info(msg);
}

MarketDataReject WsMdCore::create_reject_message(const WireMessage& msg) const {
  return mapper_.to_reject(msg);
}

WsMdCore::WireMessage WsMdCore::decode(std::string_view payload) const {
  return decoder_.decode(payload);
}

}  // namespace core
