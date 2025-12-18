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

#include "broker.h"

#include "common/ini_config.hpp"
#include "common/logger.h"
#ifdef ENABLE_WEBSOCKET
#include "src/depth_validator.h"
#endif

using common::FileSink;
using common::LogLevel;
using trading::StreamState;

Broker::Broker()
    : market_update_data_pool_(
          std::make_unique<common::MemoryPool<MarketUpdateData>>(
              kMarketUpdateDataMemoryPoolSize)),
      market_data_pool_(
          std::make_unique<common::MemoryPool<MarketData>>(kMemoryPoolSize)),
      log_(std::make_unique<common::Logger>()),
      log_producer_(log_->make_producer()) {

  INI_CONFIG.load("resources/config.ini");

  app_ = std::make_unique<SelectedMarketApp>("BMDWATCH",
      "SPOT",
      log_producer_,
      market_data_pool_.get());

  log_->setLevel(LogLevel::kInfo);
  log_->clearSink();
  log_->addSink(std::make_unique<common::FileSink>("repository",
      INI_CONFIG.get_int("log", "size")));
  log_->addSink(std::make_unique<common::ConsoleSink>());

  auto register_handler = [this](const std::string& type, auto&& handler_fn) {
    if constexpr (std::is_pointer_v<WireMessage>) {
      app_->register_callback(type,
          [handler = std::forward<decltype(handler_fn)>(handler_fn)](
              WireMessage msg) mutable { handler(msg); });
    } else {
      app_->register_callback(type,
          [handler = std::forward<decltype(handler_fn)>(handler_fn)](
              const WireMessage& msg) mutable { handler(msg); });
    }
  };

  register_handler("A", [this](auto&& msg) { on_login(msg); });
  register_handler("W", [this](auto&& msg) { on_snapshot(msg); });
  register_handler("Y", [this](auto&& msg) { on_market_request_reject(msg); });
  register_handler("1", [this](auto&& msg) { on_heartbeat(msg); });

  // Raw data callback for logging
  app_->register_callback(
      [this](auto&& str_msg, auto&& msg, auto&& event_type) {
        on_subscribe(str_msg, msg, event_type);
      });

  app_->start();
}

void Broker::on_login([[maybe_unused]] const WireMessage& msg) {
  log_producer_.info("[Broker][Login] successful");

#ifdef ENABLE_WEBSOCKET
  // WebSocket: send snapshot request
  const std::string message =
      app_->create_snapshot_request_message(INI_CONFIG.get("meta", "ticker"),
          INI_CONFIG.get("meta", "level"));
  log_producer_.info("[Broker] Snapshot request: {}", message);
  if (!app_->send(message)) {
    log_producer_.error("[Broker] failed to send snapshot request");
  }
  state_ = StreamState::kBuffering;
  buffered_events_.clear();
  first_buffered_update_id_ = 0;
#else
  // FIX: send depth subscription
  const std::string message =
      app_->create_market_data_subscription_message("DEPTH_STREAM",
          INI_CONFIG.get("meta", "level"),
          INI_CONFIG.get("meta", "ticker"),
          true);
  log_producer_.info("[Broker] Market subscription: {}", message);
  if (!app_->send(message)) {
    log_producer_.error("[Broker] failed to send market subscription");
  }
#endif
}

void Broker::on_snapshot(const WireMessage& msg) {
  log_producer_.info("[Broker] Snapshot received");

  auto* snapshot_data = market_update_data_pool_->allocate(
      app_->create_snapshot_data_message(msg));

  if (snapshot_data == nullptr) {
    log_producer_.error("[Broker] Failed to allocate snapshot");
#ifdef ENABLE_WEBSOCKET
    for (auto* buffered : buffered_events_) {
      for (auto* market_data : buffered->data)
        market_data_pool_->deallocate(market_data);
      market_update_data_pool_->deallocate(buffered);
    }
    buffered_events_.clear();
    first_buffered_update_id_ = 0;

    if (++retry_count_ >= kMaxRetries) {
      log_producer_.error("[Broker] Failed after {} retries", kMaxRetries);
      app_->stop();
      std::exit(1);
    }

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
  if (state_ == StreamState::kBuffering) {
    if (snapshot_update_id < first_buffered_update_id_) {
      log_producer_.warn("[Broker] Snapshot too old: {}, buffered: {}",
          snapshot_update_id,
          first_buffered_update_id_);

      if (++retry_count_ >= kMaxRetries) {
        log_producer_.error("[Broker] Failed after {} retries", kMaxRetries);
        app_->stop();
        std::exit(1);
      }

      std::this_thread::sleep_for(
          std::chrono::seconds(kSnapshotRetryDelaySeconds));
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

  // Log snapshot data
  log_producer_.info("[Broker] Snapshot applied: update_index={}",
      update_index_);

  // Deallocate snapshot (broker only logs, doesn't forward to trade engine)
  for (auto* market_data : snapshot_data->data)
    market_data_pool_->deallocate(market_data);
  market_update_data_pool_->deallocate(snapshot_data);

#ifdef ENABLE_WEBSOCKET
  constexpr auto kMarketType =
      trading::get_market_type<SelectedMarketApp::ExchangeTraits>();
  bool first_buffered = true;

  for (auto* buffered : buffered_events_) {
    trading::DepthValidationResult validation_result;
    if (first_buffered) {
      validation_result =
          trading::validate_first_depth_after_snapshot<kMarketType>(
              buffered->start_idx,
              buffered->end_idx,
              update_index_);
      first_buffered = false;
    } else {
      validation_result = trading::validate_continuous_depth(kMarketType,
          buffered->start_idx,
          buffered->end_idx,
          buffered->prev_end_idx,
          update_index_);
    }

    if (validation_result.valid) {
      update_index_ = validation_result.new_update_index;
      log_producer_.info("[Broker] Buffered event applied: start={}, end={}",
          buffered->start_idx,
          buffered->end_idx);
    } else {
      log_producer_.error(
          "[Broker] Buffered gap! expected pu:{}, got pu:{}, start:{}, end:{}",
          update_index_,
          buffered->prev_end_idx,
          buffered->start_idx,
          buffered->end_idx);
      buffered_events_.clear();

      if (++retry_count_ >= kMaxRetries) {
        log_producer_.error("[Broker] Failed after {} retries", kMaxRetries);
        app_->stop();
        std::exit(1);
      }

      recover_from_gap();
      return;
    }

    // Deallocate buffered
    for (auto* market_data : buffered->data)
      market_data_pool_->deallocate(market_data);
    market_update_data_pool_->deallocate(buffered);
  }
  buffered_events_.clear();
  retry_count_ = 0;
  first_depth_after_snapshot_ = true;
#endif

  state_ = StreamState::kRunning;
  log_producer_.info("[Broker] Snapshot done, state=Running");
}

void Broker::on_subscribe(const std::string& str_msg, const WireMessage& msg,
    [[maybe_unused]] const std::string& event_type) {
  log_producer_.info(str_msg);

#ifdef LIGHT_LOGGER
  subscribed_ = true;
#endif

#ifdef ENABLE_WEBSOCKET
  auto* data =
      market_update_data_pool_->allocate(app_->create_market_data_message(msg));

  if (state_ == StreamState::kBuffering) {
    if (data->type == kTrade) {
      for (auto* market_data : data->data)
        market_data_pool_->deallocate(market_data);
      market_update_data_pool_->deallocate(data);
      return;
    }

    if (first_buffered_update_id_ == 0) {
      first_buffered_update_id_ = data->start_idx;
    }

    static constexpr size_t kMaxBufferedEvents = 10;
    if (buffered_events_.size() >= kMaxBufferedEvents) {
      auto* oldest = buffered_events_.front();
      for (auto* market_data : oldest->data)
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

  // Skip gap check for trade events
  if (data->type != kTrade) {
    constexpr auto kMarketType =
        trading::get_market_type<SelectedMarketApp::ExchangeTraits>();
    trading::DepthValidationResult validation_result;

    if (first_depth_after_snapshot_) {
      validation_result =
          trading::validate_first_depth_after_snapshot<kMarketType>(
              data->start_idx,
              data->end_idx,
              update_index_);
      first_depth_after_snapshot_ = false;
    } else {
      validation_result = trading::validate_continuous_depth(kMarketType,
          data->start_idx,
          data->end_idx,
          data->prev_end_idx,
          update_index_);
    }

    if (!validation_result.valid) {
      log_producer_.error(
          "[Broker] Gap detected: expected {}, got start:{}, end:{}",
          update_index_ + 1,
          data->start_idx,
          data->end_idx);
      recover_from_gap();
      for (auto* market_data : data->data)
        market_data_pool_->deallocate(market_data);
      market_update_data_pool_->deallocate(data);
      return;
    }
    update_index_ = validation_result.new_update_index;
  }

  // Deallocate (broker only logs)
  for (auto* market_data : data->data)
    market_data_pool_->deallocate(market_data);
  market_update_data_pool_->deallocate(data);

#else
  // FIX: sequence validation
  if (!fix_seq_counter_.is_valid(str_msg)) {
    resubscribe();
  }
#endif
}

void Broker::on_market_request_reject(
    [[maybe_unused]] const WireMessage& msg) const {
  log_producer_.error("[Broker] Market subscription rejected");
}

void Broker::on_heartbeat(const WireMessage& msg) const {
  auto message = app_->create_heartbeat_message(msg);
  app_->send(message);
}

void Broker::recover_from_gap() {
  log_producer_.error("[Broker] Recovering from gap...");

#ifdef ENABLE_WEBSOCKET
  state_ = StreamState::kBuffering;
  buffered_events_.clear();
  first_buffered_update_id_ = 0;
  update_index_ = 0;

  const std::string message =
      app_->create_snapshot_request_message(INI_CONFIG.get("meta", "ticker"),
          INI_CONFIG.get("meta", "level"));
  while (!app_->send(message)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(kSleep));
  }
#else
  resubscribe();
#endif

#ifdef LIGHT_LOGGER
  subscribed_ = false;
#endif
}

void Broker::resubscribe() {
  log_producer_.error("[Broker] Resubscribing...");

#ifndef ENABLE_WEBSOCKET
  const std::string msg_unsub =
      app_->create_market_data_subscription_message("DEPTH_STREAM",
          INI_CONFIG.get("meta", "level"),
          INI_CONFIG.get("meta", "ticker"),
          /*subscribe=*/false);
  while (!app_->send(msg_unsub)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(kSleep));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(kSleep));

  const std::string msg_sub =
      app_->create_market_data_subscription_message("DEPTH_STREAM",
          INI_CONFIG.get("meta", "level"),
          INI_CONFIG.get("meta", "ticker"),
          /*subscribe=*/true);
  while (!app_->send(msg_sub)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(kSleep));
  }
#endif

#ifdef LIGHT_LOGGER
  subscribed_ = false;
#endif
}

void Broker::erase_buffer_lower_than_snapshot(uint64_t snapshot_update_id) {
#ifdef ENABLE_WEBSOCKET
  while (!buffered_events_.empty()) {
    auto* front = buffered_events_.front();
    if (front->end_idx <= snapshot_update_id) {
      for (auto* market_data : front->data)
        market_data_pool_->deallocate(market_data);
      market_update_data_pool_->deallocate(front);
      buffered_events_.pop_front();
    } else {
      break;
    }
  }

  if (!buffered_events_.empty()) {
    first_buffered_update_id_ = buffered_events_.front()->start_idx;
  } else {
    first_buffered_update_id_ = 0;
  }
#else
  (void)snapshot_update_id;
#endif
}
