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

#ifndef MARKET_CONSUMER_H
#define MARKET_CONSUMER_H

#include "logger.h"
#include "market_data.h"
#include "memory_pool.hpp"

namespace FIX8 {  // NOLINT(readability-identifier-naming)
class Message;
}

namespace core {
template <typename Derived, FixedString ReadThreadName,
          FixedString WriteThreadName>
class FixApp;
class FixMarketDataApp;
}  // namespace core

namespace trading {
class TradeEngine;

enum class StreamState : uint8_t {
  kRunning,
  kAwaitingSnapshot,
  kApplyingSnapshot
};

class MarketConsumer {
 public:
  MarketConsumer(common::Logger* logger, TradeEngine* trade_engine,
                 common::MemoryPool<MarketUpdateData>* market_update_data_pool,
                 common::MemoryPool<MarketData>* market_data_pool);
  ~MarketConsumer();
  void stop();
  void on_login(FIX8::Message*) const;
  void on_snapshot(FIX8::Message* msg);
  void on_subscribe(FIX8::Message* msg);
  void on_reject(FIX8::Message*) const;
  void on_logout(FIX8::Message*) const;
  void on_instrument_list(FIX8::Message* msg) const;
  void on_heartbeat(FIX8::Message* msg) const;
  void resubscribe();

 private:
  common::MemoryPool<MarketUpdateData>* market_update_data_pool_;
  common::MemoryPool<MarketData>* market_data_pool_;
  common::Logger* logger_;
  TradeEngine* trade_engine_;
  std::unique_ptr<core::FixMarketDataApp> app_;
  uint64_t update_index_ = 0ULL;

  std::atomic<uint64_t> generation_{0};
  std::atomic<uint64_t> current_generation_{0};
  StreamState state_{StreamState::kAwaitingSnapshot};
};
}  // namespace trading

#endif  //MARKET_CONSUMER_H