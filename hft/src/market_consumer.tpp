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
#include "fix/fix_md_app.h"
#include "market_consumer.h"
#include "trade_engine.h"

namespace trading {

template <typename Strategy, typename MdApp>
  requires core::MarketDataAppLike<MdApp>
template <typename OeApp>
MarketConsumer<Strategy, MdApp>::MarketConsumer(common::Logger* logger,
    TradeEngine<Strategy, OeApp>* trade_engine,
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

  using WireMessage = typename MdApp::WireMessage;
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

template <typename Strategy, typename MdApp>
  requires core::MarketDataAppLike<MdApp>
MarketConsumer<Strategy, MdApp>::~MarketConsumer() {
  logger_.info("[Destructor] MarketConsumer Destroy");
}

template <typename Strategy, typename MdApp>
  requires core::MarketDataAppLike<MdApp>
void MarketConsumer<Strategy, MdApp>::stop() {
  app_->stop();
}

template <typename Strategy, typename MdApp>
  requires core::MarketDataAppLike<MdApp>
void MarketConsumer<Strategy, MdApp>::on_login(WireMessage /*msg*/) {
  logger_.info("[MarketConsumer][Login] Market consumer successful");
#ifdef ENABLE_WEBSOCKET
  if constexpr (std::same_as<MdApp, core::WsMarketDataApp>) {
    const std::string message =
        app_->create_snapshot_request_message(INI_CONFIG.get("meta", "ticker"),
            INI_CONFIG.get("meta", "level"));

    if (UNLIKELY(!app_->send(message))) {
      logger_.error("[MarketConsumer][Message] failed to send login");
    }
    state_ = StreamState::kBuffering;
    buffered_events_.clear();
    first_buffered_update_id_ = 0;
  }
#else
  if constexpr (std::same_as<MdApp, core::FixMarketDataApp>) {
    const std::string message =
        app_->create_market_data_subscription_message("DEPTH_STREAM",
            INI_CONFIG.get("meta", "level"),
            INI_CONFIG.get("meta", "ticker"),
            true);

    if (UNLIKELY(!app_->send(message))) {
      logger_.error("[MarketConsumer][Message] failed to send login");
    }
  }
#endif

  const std::string instrument_message =
      app_->request_instrument_list_message(INI_CONFIG.get("meta", "ticker"));
  if (UNLIKELY(!app_->send(instrument_message))) {
    logger_.error("[MarketConsumer][Message] failed to send instrument list");
  }
}

#ifdef ENABLE_WEBSOCKET
template <typename Strategy, typename MdApp>
  requires core::MarketDataAppLike<MdApp>
void MarketConsumer<Strategy, MdApp>::erase_buffer_lower_than_snapshot(
    const uint64_t snapshot_update_id) {
  while (!buffered_events_.empty()) {
    if (const auto* event = buffered_events_.front();
        event->end_idx <= snapshot_update_id) {
      for (const auto* market_data : event->data)
        market_data_pool_->deallocate(market_data);
      market_update_data_pool_->deallocate(event);
      buffered_events_.pop_front();
    } else {
      break;
    }
  }
}
#endif
template <typename Strategy, typename MdApp>
  requires core::MarketDataAppLike<MdApp>
void MarketConsumer<Strategy, MdApp>::on_snapshot(WireMessage msg) {
  logger_.info("[MarketConsumer]Snapshot making start");

  auto* snapshot_data = market_update_data_pool_->allocate(
      app_->create_snapshot_data_message(msg));

  if (UNLIKELY(snapshot_data == nullptr)) {
    logger_.error(
        "[MarketConsumer] Market update data pool exhausted on snapshot");
#ifdef ENABLE_WEBSOCKET
    // Clear buffered events to free memory
    logger_.warn(
        "[MarketConsumer] Clearing {} buffered events to free memory",
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
      logger_.warn("[MarketConsumer][Message]Snapshot too old, refetching "
                      "snapshot:{}, buffered:{}",
              snapshot_update_id,
              first_buffered_update_id_);

      if (++retry_count_ >= kMaxRetries) {
        logger_.error("[MarketConsumer][Message]Failed to get valid snapshot "
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
        logger_.error("[MarketConsumer][Message]Failed to recover from gap "
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

#ifdef ENABLE_WEBSOCKET
template <typename Strategy, typename MdApp>
  requires core::MarketDataAppLike<MdApp>
void MarketConsumer<Strategy, MdApp>::on_subscribe(WireMessage msg) {
  auto* data =
      market_update_data_pool_->allocate(app_->create_market_data_message(msg));

  if (state_ == StreamState::kBuffering) {
    if (data->type == kTrade) {
      for (auto* md : data->data)
        market_data_pool_->deallocate(md);
      market_update_data_pool_->deallocate(data);
      return;
    }

    if (first_buffered_update_id_ == 0) {
      first_buffered_update_id_ = data->start_idx;
    }

    // Limit buffer size to prevent memory exhaustion
    static constexpr size_t kMaxBufferedEvents = 1000;
    if (buffered_events_.size() >= kMaxBufferedEvents) {
      const auto* oldest = buffered_events_.front();
      for (const auto* market_data : oldest->data)
        market_data_pool_->deallocate(market_data);
      market_update_data_pool_->deallocate(oldest);
      buffered_events_.pop_front();

      if (!buffered_events_.empty()) {
        first_buffered_update_id_ = buffered_events_.front()->start_idx;
      }
    }

    buffered_events_.push_back(data);
    return;
  }

  // Skip gap check for trade events (they don't have sequence numbers)
  if (data->type != kTrade) {
    logger_.trace("current update index:{}, data start :{}, data end:{}",
        update_index_,
        data->start_idx,
        data->end_idx);
    if (data->start_idx != update_index_ + 1 && update_index_ != 0) {
      logger_.error("Gap detected");
      recover_from_gap();
      for (auto* md : data->data)
        market_data_pool_->deallocate(md);
      market_update_data_pool_->deallocate(data);
      return;
    }
    update_index_ = data->end_idx;
  }

  on_market_data_fn_(data);
}
#else
template <typename Strategy, typename MdApp>
  requires core::MarketDataAppLike<MdApp>
void MarketConsumer<Strategy, MdApp>::on_subscribe(WireMessage msg) {
  auto* data =
      market_update_data_pool_->allocate(app_->create_market_data_message(msg));

  if (UNLIKELY(data == nullptr)) {
    logger_.error(
        "[Error] Failed to allocate market data message, but log is here");
#ifdef NDEBUG
    app_->stop();
    exit(1);
#endif
    return;
  }

  if (UNLIKELY(state_ == StreamState::kAwaitingSnapshot)) {
    logger_.info("Waiting for making snapshot");
    return;
  }

  if (UNLIKELY((data->type == kNone) ||
               (data->type == kMarket &&
                   data->start_idx != this->update_index_ + 1 &&
                   this->update_index_ != 0ULL))) {
    logger_.error(
        "Update index is outdated. current index :{}, new index :{}",
        this->update_index_,
        data->start_idx);

    resubscribe();

    for (const auto& market_data : data->data) {
      market_data_pool_->deallocate(market_data);
    }
    market_update_data_pool_->deallocate(data);
    return;
  }

  this->update_index_ = data->end_idx;
  if (UNLIKELY(!on_market_data_fn_(data))) {
    logger_.error("[Message] failed to send subscribe");
  }
}

template <typename Strategy, typename MdApp>
  requires core::MarketDataAppLike<MdApp>
void MarketConsumer<Strategy, MdApp>::resubscribe() {
  logger_.info("Try resubscribing");
  current_generation_.store(
      generation_.fetch_add(1, std::memory_order_acq_rel) + 1,
      std::memory_order_release);

  const std::string msg_unsub =
      app_->create_market_data_subscription_message("DEPTH_STREAM",
          INI_CONFIG.get("meta", "level"),
          INI_CONFIG.get("meta", "ticker"),
          /*subscribe=*/false);
  app_->send(msg_unsub);

  std::this_thread::sleep_for(std::chrono::seconds(5));

  const std::string msg_sub =
      app_->create_market_data_subscription_message("DEPTH_STREAM",
          INI_CONFIG.get("meta", "level"),
          INI_CONFIG.get("meta", "ticker"),
          /*subscribe=*/true);
  app_->send(msg_sub);

  ++generation_;
  state_ = StreamState::kAwaitingSnapshot;
  update_index_ = 0ULL;
}
#endif

template <typename Strategy, typename MdApp>
  requires core::MarketDataAppLike<MdApp>
void MarketConsumer<Strategy, MdApp>::on_reject(WireMessage msg) {
  const auto rejected_message = app_->create_reject_message(msg);
  logger_.error("[MarketConsumer][Message] {}", rejected_message.toString());
  if (rejected_message.session_reject_reason == "A") {
    app_->stop();
  }
}

template <typename Strategy, typename MdApp>
  requires core::MarketDataAppLike<MdApp>
void MarketConsumer<Strategy, MdApp>::on_logout(WireMessage /*msg*/) {
  logger_.info("[MarketConsumer][Message] logout");
}

template <typename Strategy, typename MdApp>
  requires core::MarketDataAppLike<MdApp>
void MarketConsumer<Strategy, MdApp>::on_instrument_list(WireMessage msg) {
  const InstrumentInfo instrument_message =
      app_->create_instrument_list_message(msg);
  logger_.info("[MarketConsumer][Message] on_instrument_list :{}",
      instrument_message.toString());
  on_instrument_info_fn_(instrument_message);
}

template <typename Strategy, typename MdApp>
  requires core::MarketDataAppLike<MdApp>
void MarketConsumer<Strategy, MdApp>::on_heartbeat(WireMessage msg) {
  auto message = app_->create_heartbeat_message(msg);
  if (UNLIKELY(!app_->send(message))) {
    logger_.error("[MarketConsumer][Message] failed to send heartbeat");
  }
}

template <typename Strategy, typename MdApp>
  requires core::MarketDataAppLike<MdApp>
void MarketConsumer<Strategy, MdApp>::recover_from_gap() {
#ifdef ENABLE_WEBSOCKET
  if (state_ == StreamState::kBuffering) {
    logger_.info(
        "[MarketConsumer]Gap detected, but already snapshot buffering mode.");
    return;
  }
  logger_.info("[MarketConsumer]Gap detected, entering buffering mode");
  state_ = StreamState::kBuffering;
  buffered_events_.clear();
  first_buffered_update_id_ = 0;

  const std::string snapshot_req =
      app_->create_snapshot_request_message(INI_CONFIG.get("meta", "ticker"),
          INI_CONFIG.get("meta", "level"));
  app_->send(snapshot_req);

  logger_.info("Gap detected, resubscribing");
#else
  current_generation_.fetch_add(1, std::memory_order_acq_rel);

  const std::string msg_unsub =
      app_->create_market_data_subscription_message("DEPTH_STREAM",
          INI_CONFIG.get("meta", "level"),
          INI_CONFIG.get("meta", "ticker"),
          false);
  app_->send(msg_unsub);

  std::this_thread::sleep_for(std::chrono::seconds(5));

  const std::string msg_sub =
      app_->create_market_data_subscription_message("DEPTH_STREAM",
          INI_CONFIG.get("meta", "level"),
          INI_CONFIG.get("meta", "ticker"),
          true);
  app_->send(msg_sub);

  state_ = StreamState::kAwaitingSnapshot;
  update_index_ = 0ULL;
#endif
}

}  // namespace trading

#endif  // MARKET_CONSUMER_TPP
