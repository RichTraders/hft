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

#include "common/logger.h"
#include "common/memory_pool.hpp"
#include "core/market_data.h"
#include "ws_md_decoder.h"
#include "ws_md_domain_mapper.h"
#include "ws_md_encoder.h"
#include "ws_md_wire_message.h"

namespace core {
class WsMdCore {
 public:
  using WireMessage = WsMdWireMessage;
  using RequestId = std::string;
  using MarketDepthLevel = std::string;
  using SymbolId = std::string;

  WsMdCore(common::Logger* logger, common::MemoryPool<MarketData>* pool);

  [[nodiscard]] std::string create_market_data_subscription_message(
      const RequestId& request_id, const MarketDepthLevel& level,
      const SymbolId& symbol, bool subscribe) const;
  [[nodiscard]] std::string create_trade_data_subscription_message(
      const RequestId& request_id, const MarketDepthLevel& level,
      const SymbolId& symbol) const;
  [[nodiscard]] std::string request_instrument_list_message(
      const std::string& symbol) const;
  [[nodiscard]] MarketUpdateData create_market_data_message(
      const WireMessage& msg) const;
  [[nodiscard]] MarketUpdateData create_snapshot_data_message(
      const WireMessage& msg) const;
  [[nodiscard]] InstrumentInfo create_instrument_list_message(
      const WireMessage& msg) const;
  [[nodiscard]] MarketDataReject create_reject_message(
      const WireMessage& msg) const;
  [[nodiscard]] WireMessage decode(std::string_view payload) const;

 private:
  common::Logger::Producer logger_;
  WsMdDecoder decoder_;
  WsMdDomainMapper mapper_;
  WsMdEncoder encoder_;
  common::MemoryPool<MarketData>* market_data_pool_;
};

}  // namespace core

#endif
