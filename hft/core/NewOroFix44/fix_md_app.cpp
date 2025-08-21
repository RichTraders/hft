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

#include "fix_md_app.h"
#include "fix_md_core.h"
#include "authorization.h"

namespace core {

FixMarketDataApp::FixMarketDataApp(const std::string& sender_comp_id,
                   const std::string& target_comp_id, common::Logger* logger,
                   common::MemoryPool<MarketData>* market_data_pool):
    FixApp(AUTHORIZATION.get_md_address(),
           AUTHORIZATION.get_port(),
           sender_comp_id,
           target_comp_id,
           logger)
    , market_data_pool_(market_data_pool) {
  fix_md_core_ = std::make_unique<FixMdCore>(sender_comp_id, target_comp_id,
                                             logger, market_data_pool);
}

FixMarketDataApp::~FixMarketDataApp() {

}

std::string FixMarketDataApp::create_log_on_message(
    const std::string& sig_b64, const std::string& timestamp) {
  return fix_md_core_->create_log_on_message(sig_b64, timestamp);
}

std::string FixMarketDataApp::create_log_out_message() {
  return fix_md_core_->create_log_out_message();
}

std::string FixMarketDataApp::create_heartbeat_message(FIX8::Message* message) {
  return fix_md_core_->create_heartbeat_message(message);
}

std::string FixMarketDataApp::create_market_data_subscription_message(
    const RequestId& request_id, const MarketDepthLevel& level,
    const SymbolId& symbol) {
  return fix_md_core_->create_market_data_subscription_message(request_id,
    level, symbol);
}

std::string FixMarketDataApp::create_trade_data_subscription_message(
    const RequestId& request_id, const MarketDepthLevel& level,
    const SymbolId& symbol) {
  return fix_md_core_->create_trade_data_subscription_message(request_id, level,
    symbol);
}

MarketUpdateData FixMarketDataApp::create_market_data_message(
    FIX8::Message* msg) {
  return fix_md_core_->create_market_data_message(msg);
}

MarketUpdateData FixMarketDataApp::create_snapshot_data_message(
    FIX8::Message* msg) {
  return fix_md_core_->create_snapshot_data_message(msg);
}

FIX8::Message* FixMarketDataApp::decode(const std::string& message) {
  return fix_md_core_->decode(message);
}
} // namespace core