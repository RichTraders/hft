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

#ifndef BROKER_H
#define BROKER_H

#include "common/logger.h"
#include "common/memory_pool.hpp"
#include "src/stream_state.h"

#ifdef ENABLE_WEBSOCKET
#include "core/websocket/market_data/ws_md_app.hpp"
using SelectedMarketApp = core::WsMarketDataApp;
#else
#include "core/fix/fix_md_app.h"
#include "core/fix/fix_sequence_counter.h"
using SelectedMarketApp = core::FixMarketDataApp;
#endif

class Broker {
 public:
  Broker();

 private:
  using WireMessage = SelectedMarketApp::WireMessage;

  static constexpr int kMemoryPoolSize = 65536;
  static constexpr int kMarketUpdateDataMemoryPoolSize = 1024;
  static constexpr int kSleep = 10;
  static constexpr int kSnapshotRetryDelaySeconds = 10;
  static constexpr int kMaxRetries = 3;

  std::unique_ptr<common::MemoryPool<MarketUpdateData>>
      market_update_data_pool_;
  std::unique_ptr<common::MemoryPool<MarketData>> market_data_pool_;
  std::unique_ptr<common::Logger> log_;
  common::Logger::Producer log_producer_;
  std::unique_ptr<SelectedMarketApp> app_;

  uint64_t update_index_ = 0;
  trading::StreamState state_{trading::StreamState::kAwaitingSnapshot};
  int retry_count_{0};

#ifdef ENABLE_WEBSOCKET
  std::deque<MarketUpdateData*> buffered_events_;
  uint64_t first_buffered_update_id_{0};
  bool first_depth_after_snapshot_{false};
#else
  core::FixSequenceCounter fix_seq_counter_;
#endif

#ifdef LIGHT_LOGGER
  bool subscribed_ = false;
#endif

  void on_login(const WireMessage& msg);
  void on_snapshot(const WireMessage& msg);
  void on_subscribe(const std::string& str_msg, const WireMessage& msg,
      const std::string& event_type);
  void on_market_request_reject(const WireMessage& msg) const;
  void on_heartbeat(const WireMessage& msg) const;

  void recover_from_gap();
  void resubscribe();
  void erase_buffer_lower_than_snapshot(uint64_t snapshot_update_id);
};

#endif  //BROKER_H
