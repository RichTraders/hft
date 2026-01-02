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

#ifndef FIX_MD_APP_H
#define FIX_MD_APP_H

#include <memory>
#include <string>

#include "common/memory_pool.hpp"
#include "core/market_data.h"
#include "core/protocol_concepts.h"
#include "fix_app.h"
#include "fix_md_core.h"

namespace FIX8 {
class Message;
}

namespace core {
class FixMdCore;

class FixMarketDataApp : public FixApp<FixMarketDataApp, "MDRead", "MDWrite"> {
 public:
  using WireMessage = FixMdCore::WireMessage;

  FixMarketDataApp(const std::string& sender_comp_id,
      const std::string& target_comp_id,
      const common::Logger::Producer& logger,
      common::MemoryPool<MarketData>* market_data_pool);

  ~FixMarketDataApp();

  std::string create_log_on_message(const std::string& sig_b64,
      const std::string& timestamp) const;
  std::string create_log_out_message() const;
  std::string create_heartbeat_message(WireMessage message) const;
  [[nodiscard]] std::string create_market_data_subscription_message(
      const RequestId& request_id, const MarketDepthLevel& level,
      const SymbolId& symbol, bool subscribe) const;
  std::string create_trade_data_subscription_message(
      const RequestId& request_id, const MarketDepthLevel& level,
      const SymbolId& symbol, bool subscribe) const;
  MarketUpdateData create_market_data_message(WireMessage msg) const;
  MarketUpdateData create_snapshot_data_message(WireMessage msg) const;
  std::string request_instrument_list_message(const std::string& symbol = "") const;
  InstrumentInfo create_instrument_list_message(WireMessage msg) const;
  MarketDataReject create_reject_message(WireMessage msg) const;
  WireMessage decode(const std::string& message) const;

 private:
  std::unique_ptr<FixMdCore> fix_md_core_;
};

// static_assert(core::MarketDataCore<FixMdCore>,
//     "FixMdCore must satisfy the MarketDataCore concept");
}  // namespace core

#endif
