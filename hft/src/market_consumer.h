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
#include "market_data_protocol_policy.h"
#include "memory_pool.hpp"
#include "protocol_impl.h"
#include "stream_state.h"

namespace trading {
template <typename Strategy>
class TradeEngine;

template <typename Derived>
class MarketConsumerRecoveryMixin;

template <typename Strategy>
class MarketConsumer
    : public MarketConsumerRecoveryMixin<MarketConsumer<Strategy>> {
  friend class MarketConsumerRecoveryMixin<MarketConsumer>;

 public:
  using MdApp = protocol_impl::MarketDataApp;
  using AppType = MdApp;
  using ProtocolPolicy = typename MarketDataProtocolPolicySelector<MdApp>::type;
  using WireMessage = MdApp::WireMessage;

  MarketConsumer(common::Logger* logger, TradeEngine<Strategy>* trade_engine,
      common::MemoryPool<MarketUpdateData>* market_update_data_pool,
      common::MemoryPool<MarketData>* market_data_pool);
  ~MarketConsumer();
  void stop();
  void on_login(WireMessage msg);
  void on_snapshot(WireMessage msg);
  void on_subscribe(WireMessage msg);
  void on_reject(WireMessage msg) const;
  void on_logout(WireMessage msg) const;
  void on_instrument_list(WireMessage msg) const;
  void on_heartbeat(WireMessage msg) const;

  // Recovery methods (implementation in CRTP base)
  void recover_from_gap() { this->recover_from_gap_impl(); }
  void erase_buffer_lower_than_snapshot(uint64_t snapshot_update_id) {
    this->erase_buffer_lower_than_snapshot_impl(snapshot_update_id);
  }
  void resubscribe() { this->resubscribe_impl(); }

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

#include "market_consumer_recovery.hpp"

#endif  //MARKET_CONSUMER_H
