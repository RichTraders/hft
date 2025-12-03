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
#include "protocol_concepts.h"

#ifdef ENABLE_WEBSOCKET

#endif

namespace core {
#ifdef ENABLE_WEBSOCKET
class WsMarketDataApp;
//class WsOrderEntryApp;
#else
class FixMarketDataApp;
class FixOrderEntryApp;
template <typename Derived, FixedString ReadThreadName,
    FixedString WriteThreadName>
class FixApp;
#endif
}  // namespace core

namespace trading {
template <typename Strategy, typename App>
class TradeEngine;

enum class StreamState : uint8_t {
  kRunning,
  kAwaitingSnapshot,
  kApplyingSnapshot,
  kBuffering,
};

template <typename Strategy, typename MdApp>
  requires core::MarketDataAppLike<MdApp>
class MarketConsumer {
 public:
  using AppType = MdApp;
  using WireMessage = typename MdApp::WireMessage;

  template <typename OeApp>
  MarketConsumer(common::Logger* logger,
      TradeEngine<Strategy, OeApp>* trade_engine,
      common::MemoryPool<MarketUpdateData>* market_update_data_pool,
      common::MemoryPool<MarketData>* market_data_pool);
  ~MarketConsumer();
  void stop();
  void on_login(WireMessage msg);
  void on_snapshot(WireMessage msg);
  void on_subscribe(WireMessage msg);
  void on_reject(WireMessage msg);
  void on_logout(WireMessage msg);
  void on_instrument_list(WireMessage msg);
  void on_heartbeat(WireMessage msg);

#ifndef ENABLE_WEBSOCKET
  void resubscribe();
#else
  void recover_from_gap();
  void erase_buffer_lower_than_snapshot(uint64_t snapshot_update_id);
#endif

 private:
  common::MemoryPool<MarketUpdateData>* market_update_data_pool_;
  common::MemoryPool<MarketData>* market_data_pool_;
  common::Logger::Producer logger_;
  std::function<bool(MarketUpdateData*)> on_market_data_fn_;
  std::function<void(const InstrumentInfo&)> on_instrument_info_fn_;
  std::unique_ptr<MdApp> app_;
  uint64_t update_index_ = 0ULL;

  StreamState state_{StreamState::kAwaitingSnapshot};
  int retry_count_{0};
#ifdef ENABLE_WEBSOCKET
  std::deque<MarketUpdateData*> buffered_events_;
  uint64_t first_buffered_update_id_{0};
#else
  std::atomic<uint64_t> generation_{0};
  std::atomic<uint64_t> current_generation_{0};
#endif
};
}  // namespace trading
#endif  //MARKET_CONSUMER_H
