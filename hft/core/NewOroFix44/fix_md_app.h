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

#pragma once

#include "fix_app.h"
#include "market_data.h"
#include "memory_pool.hpp"

#include <fix_app.h>

namespace FIX8 {
class Message;
}

namespace core {
class FixMdCore;

class FixMarketDataApp : public FixApp<FixMarketDataApp, "MDRead", "MDWrite"> {
 public:
  FixMarketDataApp(const std::string& sender_comp_id,
                   const std::string& target_comp_id, common::Logger* logger,
                   common::MemoryPool<MarketData>* market_data_pool);

  ~FixMarketDataApp();

  std::string create_log_on_message(const std::string& sig_b64,
                                    const std::string& timestamp);
  std::string create_log_out_message();
  std::string create_heartbeat_message(FIX8::Message* message);
  [[nodiscard]] std::string create_market_data_subscription_message(
      const RequestId& request_id, const MarketDepthLevel& level,
      const SymbolId& symbol, bool subscribe) const;
  std::string create_trade_data_subscription_message(
      const RequestId& request_id, const MarketDepthLevel& level,
      const SymbolId& symbol);
  MarketUpdateData create_market_data_message(FIX8::Message* msg);
  MarketUpdateData create_snapshot_data_message(FIX8::Message* msg);
  std::string request_instrument_list_message();
  InstrumentInfo create_instrument_list_message(FIX8::Message* msg);
  MarketDataReject create_reject_message(FIX8::Message* msg);
  FIX8::Message* decode(const std::string& message);

 private:
  common::MemoryPool<MarketData>* market_data_pool_;
  std::unique_ptr<FixMdCore> fix_md_core_;
};
}  // namespace core