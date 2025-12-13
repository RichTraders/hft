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

#ifndef MARKET_CONSUMER_TPP
#define MARKET_CONSUMER_TPP

#include "common/ini_config.hpp"
#include "market_consumer.h"
#include "trade_engine.h"

namespace trading {

template <typename Strategy>
MarketConsumer<Strategy>::MarketConsumer(common::Logger* logger,
    TradeEngine<Strategy>* trade_engine,
    common::MemoryPool<MarketUpdateData>* market_update_data_pool,
    common::MemoryPool<MarketData>* market_data_pool)
    : market_update_data_pool_(market_update_data_pool),
      market_data_pool_(market_data_pool),
      logger_(logger->make_producer()),
      on_market_data_fn_([trade_engine](MarketUpdateData* data) {
        return trade_engine->on_market_data_updated(data);
      }),
      on_instrument_info_fn_([trade_engine](const InstrumentInfo& info) {
        trade_engine->on_instrument_info(info);
      }),
      app_(std::make_unique<MdApp>("BMDWATCH", "SPOT", logger,
          market_data_pool_)) {

  using WireMessage = MdApp::WireMessage;
  auto register_handler = [this](const std::string& type, auto&& fn) {
    if constexpr (std::is_pointer_v<WireMessage>) {
      app_->register_callback(type,
          [handler = std::forward<decltype(fn)>(fn)](
              WireMessage msg) mutable { handler(msg); });
    } else {
      app_->register_callback(type,
          [handler = std::forward<decltype(fn)>(fn)](
              const WireMessage& msg) mutable { handler(msg); });
    }
  };

  register_handler("A", [this](auto&& msg) { on_login(msg); });
  register_handler("W", [this](auto&& msg) { on_snapshot(msg); });
  register_handler("X", [this](auto&& msg) { on_subscribe(msg); });
  register_handler("1", [this](auto&& msg) { on_heartbeat(msg); });
  register_handler("y", [this](auto&& msg) { on_instrument_list(msg); });
  register_handler("3", [this](auto&& msg) { on_reject(msg); });
  register_handler("5", [this](auto&& msg) { on_logout(msg); });

  app_->start();
  logger_.info("[Constructor] MarketConsumer Created");
}

template <typename Strategy>
MarketConsumer<Strategy>::~MarketConsumer() {
  std::cout << "[Destructor] MarketConsumer Destroy\n";
}

template <typename Strategy>
void MarketConsumer<Strategy>::stop() {
  app_->stop();
}

template <typename Strategy>
void MarketConsumer<Strategy>::on_login(WireMessage msg) {
#ifdef ENABLE_WEBSOCKET
  ProtocolPolicy::handle_login(*app_, msg, state_, buffered_events_,
      first_buffered_update_id_, logger_);
#else
  std::deque<MarketUpdateData*> dummy_buffered;
  uint64_t dummy_first_buffered = 0;
  ProtocolPolicy::handle_login(*app_, msg, state_, dummy_buffered,
      dummy_first_buffered, logger_);
#endif
}

template <typename Strategy>
void MarketConsumer<Strategy>::on_snapshot(WireMessage msg) {
  logger_.info("[MarketConsumer]Snapshot making start");

  auto* snapshot_data = market_update_data_pool_->allocate(
      app_->create_snapshot_data_message(msg));

  if (UNLIKELY(snapshot_data == nullptr)) {
    logger_.error(
        "[MarketConsumer] Market update data pool exhausted on snapshot");
#ifdef ENABLE_WEBSOCKET
    logger_.warn("[MarketConsumer] Clearing {} buffered events to free memory",
        buffered_events_.size());
    for (auto* buffered : buffered_events_) {
      for (auto* market_data : buffered->data)
        market_data_pool_->deallocate(market_data);
      market_update_data_pool_->deallocate(buffered);
    }
    buffered_events_.clear();
    first_buffered_update_id_ = 0;

    static constexpr int kMaxRetries = 3;
    if (++retry_count_ >= kMaxRetries) {
      logger_.error(
          "[MarketConsumer] Failed to allocate snapshot after {} retries",
          kMaxRetries);
      app_->stop();
      std::exit(1);
    }

    // Request snapshot again
    std::this_thread::sleep_for(std::chrono::seconds(1));
    const std::string snapshot_req =
        app_->create_snapshot_request_message(INI_CONFIG.get("meta", "ticker"),
            INI_CONFIG.get("meta", "level"));
    app_->send(snapshot_req);
#endif
    return;
  }

  const uint64_t snapshot_update_id = snapshot_data->end_idx;

#ifdef ENABLE_WEBSOCKET
  static constexpr int kMaxRetries = 3;
  if (state_ == StreamState::kBuffering) {
    if (snapshot_update_id < first_buffered_update_id_) {
      logger_.warn(
          "[MarketConsumer][Message]Snapshot too old, refetching "
          "snapshot:{}, buffered:{}",
          snapshot_update_id,
          first_buffered_update_id_);

      if (++retry_count_ >= kMaxRetries) {
        logger_.error(
            "[MarketConsumer][Message]Failed to get valid snapshot "
            "after {} retries, terminating",
            kMaxRetries);
        app_->stop();
        std::exit(1);
      }

      // Retry
      std::this_thread::sleep_for(std::chrono::seconds(10));

      const std::string snapshot_req = app_->create_snapshot_request_message(
          INI_CONFIG.get("meta", "ticker"),
          INI_CONFIG.get("meta", "level"));
      app_->send(snapshot_req);
      return;
    }

    retry_count_ = 0;

    erase_buffer_lower_than_snapshot(snapshot_update_id);
  }
#endif

  state_ = StreamState::kApplyingSnapshot;
  update_index_ = snapshot_update_id;
  if (UNLIKELY(!on_market_data_fn_(snapshot_data))) {
    logger_.error("[MarketConsumer][Message] failed to send snapshot");
  }

#ifdef ENABLE_WEBSOCKET
  for (auto* buffered : buffered_events_) {
    if (buffered->start_idx == update_index_ + 1) {
      update_index_ = buffered->end_idx;
      on_market_data_fn_(buffered);
    } else {
      logger_.error(
          "[MarketConsumer]Buffered event gap detected! Expected {}, got {}",
          update_index_ + 1,
          buffered->start_idx);
      buffered_events_.clear();

      if (++retry_count_ >= kMaxRetries) {
        logger_.error(
            "[MarketConsumer][Message]Failed to recover from gap "
            "after {} retries, terminating",
            kMaxRetries);
        app_->stop();
        std::exit(1);
      }

      recover_from_gap();
      return;
    }
  }
  buffered_events_.clear();
  retry_count_ = 0;
#else
  if (UNLIKELY(snapshot_data == nullptr)) {
    logger_.error("[Message] failed to create snapshot");
    resubscribe();

    for (auto& market_data : snapshot_data->data) {
      market_data_pool_->deallocate(market_data);
    }
    market_update_data_pool_->deallocate(snapshot_data);
    return;
  }
#endif

  state_ = StreamState::kRunning;
  logger_.info("[MarketConsumer]Snapshot Done");
}

template <typename Strategy>
void MarketConsumer<Strategy>::on_subscribe(WireMessage msg) {
#ifdef ENABLE_WEBSOCKET
  ProtocolPolicy::handle_subscribe(*app_, msg, state_, buffered_events_,
      first_buffered_update_id_, update_index_, on_market_data_fn_,
      market_update_data_pool_, market_data_pool_, logger_,
      [this]() { recover_from_gap(); });
#else
  std::deque<MarketUpdateData*> dummy_buffered;
  uint64_t dummy_first_buffered = 0;
  ProtocolPolicy::handle_subscribe(*app_, msg, state_, dummy_buffered,
      dummy_first_buffered, update_index_, on_market_data_fn_,
      market_update_data_pool_, market_data_pool_, logger_,
      [this]() { resubscribe(); });
#endif
}

// resubscribe implementation moved to MarketConsumerRecoveryMixin

template <typename Strategy>
void MarketConsumer<Strategy>::on_reject(WireMessage msg) const {
  const auto rejected_message = app_->create_reject_message(msg);
  logger_.error("[MarketConsumer][Message] {}", rejected_message.toString());
  if (rejected_message.session_reject_reason == "A") {
    app_->stop();
  }
}

template <typename Strategy>
void MarketConsumer<Strategy>::on_logout(WireMessage /*msg*/) const {
  logger_.info("[MarketConsumer][Message] logout");
}

template <typename Strategy>
void MarketConsumer<Strategy>::on_instrument_list(WireMessage msg) const {
  const InstrumentInfo instrument_message =
      app_->create_instrument_list_message(msg);
  logger_.info("[MarketConsumer][Message] on_instrument_list :{}",
      instrument_message.toString());
  on_instrument_info_fn_(instrument_message);
}

template <typename Strategy>
void MarketConsumer<Strategy>::on_heartbeat(WireMessage msg) const {
  auto message = app_->create_heartbeat_message(msg);
  if (UNLIKELY(!app_->send(message))) {
    logger_.error("[MarketConsumer][Message] failed to send heartbeat");
  }
}

// recover_from_gap implementation moved to MarketConsumerRecoveryMixin

}  // namespace trading

#endif  // MARKET_CONSUMER_TPP
