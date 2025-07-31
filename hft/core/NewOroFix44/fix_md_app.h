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
#include "fix_md_core.h"

namespace core {
class FixMarketDataApp : public FixApp<FixMarketDataApp, 1> {
public:
  FixMarketDataApp(const std::string& address, int port,
                   const std::string& sender_comp_id,
                   const std::string& target_comp_id, common::Logger* logger,
                   common::MemoryPool<MarketData>* market_data_pool):
    FixApp(address,
           port,
           sender_comp_id,
           target_comp_id,
           logger)
    , market_data_pool_(market_data_pool) {
    fix_md_core_ = std::make_unique<FixMdCore>(sender_comp_id, target_comp_id,
                                               logger, market_data_pool);
  }

  std::string create_log_on_message(const std::string& sig_b64,
                                    const std::string& timestamp);
  std::string create_log_out_message();
  std::string create_heartbeat_message(FIX8::Message* message);
  std::string create_market_data_subscription_message(
      const RequestId& request_id,
      const MarketDepthLevel& level,
      const SymbolId& symbol);
  std::string create_trade_data_subscription_message(
    const RequestId& request_id, const MarketDepthLevel& level,
    const SymbolId& symbol);
  MarketUpdateData create_market_data_message(FIX8::Message* msg);
  MarketUpdateData create_snapshot_data_message(FIX8::Message* msg);
  FIX8::Message* decode(const std::string& message);
private:
  common::MemoryPool<MarketData>* market_data_pool_;
  std::unique_ptr<FixMdCore> fix_md_core_;
};
}