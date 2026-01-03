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

#ifndef WS_MD_CORE_H
#define WS_MD_CORE_H

#include <string>
#include <string_view>

#include "common/logger.h"
#include "common/memory_pool.hpp"
#include "core/market_data.h"
#include "ws_md_domain_mapper.h"
#include "ws_md_encoder.hpp"

namespace core {
template <ExchangeTraits Exchange, typename DecoderType>
class WsMdCore {
 public:
  using ExchangeTraits = Exchange;
  using Decoder = DecoderType;
  using WireMessage = typename Decoder::WireMessage;
  using RequestId = std::string_view;
  using MarketDepthLevel = std::string_view;
  using SymbolId = std::string_view;

  WsMdCore(const common::Logger::Producer& logger,
      common::MemoryPool<MarketData>* pool)
      : logger_(logger),
        decoder_(logger_),
        mapper_(logger_, pool),
        encoder_(logger_) {}

  [[nodiscard]] std::string create_market_data_subscription_message(
      const RequestId& request_id, const MarketDepthLevel& level,
      const SymbolId& symbol, bool subscribe) const {
    return encoder_.create_market_data_subscription_message(request_id,
        level,
        symbol,
        subscribe);
  }

  [[nodiscard]] std::string create_trade_data_subscription_message(
      const RequestId& request_id, const MarketDepthLevel& level,
      const SymbolId& symbol, bool subscribe) const {
    return encoder_.create_trade_data_subscription_message(request_id,
        level,
        symbol,
        subscribe);
  }

  [[nodiscard]] std::string create_snapshot_data_subscription_message(
      const SymbolId& symbol, const MarketDepthLevel& level) const {
    return encoder_.create_snapshot_data_subscription_message(level, symbol);
  }

  [[nodiscard]] std::string request_instrument_list_message(
      const std::string& symbol) const {
    return encoder_.request_instrument_list_message(symbol);
  }

  [[nodiscard]] MarketUpdateData create_market_data_message(
      const WireMessage& msg) const {
    return mapper_.to_market_data(msg);
  }

  [[nodiscard]] MarketUpdateData create_snapshot_data_message(
      const WireMessage& msg) const {
    return mapper_.to_snapshot_data(msg);
  }

  [[nodiscard]] InstrumentInfo create_instrument_list_message(
      const WireMessage& msg) const {
    return mapper_.to_instrument_info(msg);
  }

  [[nodiscard]] MarketDataReject create_reject_message(
      const WireMessage& msg) const {
    return mapper_.to_reject(msg);
  }

  [[nodiscard]] WireMessage decode(std::string_view payload) const {
    return decoder_.decode(payload);
  }

 private:
  const common::Logger::Producer& logger_;
  Decoder decoder_;
  WsMdDomainMapper<Exchange, Decoder> mapper_;
  WsMdEncoder<Exchange> encoder_;
};

}  // namespace core

#endif