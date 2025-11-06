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

#include <atomic>
#include <memory>
#include <string>

#include "common/logger.h"
#include "core/NewOroFix44/fix_md_app.h"
#include "market_data_gateway_interface.h"

namespace FIX8 {
class Message;
}

namespace trading {
class TradeEngine;

enum class StreamState : uint8_t {
  kRunning,
  kAwaitingSnapshot,
  kApplyingSnapshot
};

/**
 * @brief FIX protocol market data gateway implementation
 *
 * This gateway wraps the FixMarketDataApp and provides market data
 * subscription functionality via the FIX protocol over TLS.
 */
class FixMarketDataGateway : public IMarketDataGateway {
 public:
  FixMarketDataGateway(const std::string& sender_comp_id,
                       const std::string& target_comp_id, common::Logger* logger,
                       TradeEngine* trade_engine,
                       common::MemoryPool<MarketUpdateData>* market_update_data_pool,
                       common::MemoryPool<MarketData>* market_data_pool);
  ~FixMarketDataGateway() override;

  void subscribe_market_data(const std::string& req_id, const std::string& depth,
                             const std::string& symbol, bool subscribe) override;
  void request_instrument_list(const std::string& symbol) override;
  void stop() override;

  void resubscribe();

 private:
  // FIX message callbacks
  void on_login(FIX8::Message* msg);
  void on_snapshot(FIX8::Message* msg);
  void on_subscribe(FIX8::Message* msg);
  void on_reject(FIX8::Message* msg);
  void on_logout(FIX8::Message* msg);
  void on_instrument_list(FIX8::Message* msg);
  void on_heartbeat(FIX8::Message* msg);

  common::MemoryPool<MarketUpdateData>* market_update_data_pool_;
  common::MemoryPool<MarketData>* market_data_pool_;
  common::Logger::Producer logger_;
  TradeEngine* trade_engine_;
  std::unique_ptr<core::FixMarketDataApp> app_;
  uint64_t update_index_{0};

  std::atomic<uint64_t> generation_{0};
  std::atomic<uint64_t> current_generation_{0};
  StreamState state_{StreamState::kAwaitingSnapshot};

  // Store subscription parameters for resubscribe
  std::string req_id_;
  std::string depth_;
  std::string symbol_;
};

}  // namespace trading
