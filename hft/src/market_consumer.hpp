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

#ifndef MARKET_CONSUMER_HPP
#define MARKET_CONSUMER_HPP

#include <chrono>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <thread>

#include "common/ini_config.hpp"
#include "logger.h"
#include "market_data.h"
#include "market_data_protocol_policy.h"
#include "memory_pool.hpp"
#include "protocol_impl.h"
#include "stream_state.h"
#include "trade_engine.hpp"

namespace trading {
template <typename Strategy>
class TradeEngine;

template <typename Derived>
class MarketConsumerRecoveryMixin;

template <typename Strategy, typename MdApp = protocol_impl::MarketDataApp>
class MarketConsumer
    : public MarketConsumerRecoveryMixin<MarketConsumer<Strategy, MdApp>> {
  friend class MarketConsumerRecoveryMixin<MarketConsumer>;

 public:
  using AppType = MdApp;
  using ProtocolPolicy = typename MarketDataProtocolPolicySelector<MdApp>::type;
  using WireMessage = MdApp::WireMessage;

  MarketConsumer(const common::Logger::Producer& logger,
      TradeEngine<Strategy>* trade_engine,
      common::MemoryPool<MarketUpdateData>* market_update_data_pool,
      common::MemoryPool<MarketData>* market_data_pool)
      : market_update_data_pool_(market_update_data_pool),
        market_data_pool_(market_data_pool),
        logger_(logger),
        on_market_data_fn_([trade_engine](MarketUpdateData* data) {
          return trade_engine->on_market_data_updated(data);
        }),
        on_instrument_info_fn_([trade_engine](const InstrumentInfo& info) {
          trade_engine->on_instrument_info(info);
        }),
        app_(std::make_unique<MdApp>("BMDWATCH", "SPOT", logger_,
            market_data_pool_)) {

    using WireMessage = MdApp::WireMessage;
    // NOLINTNEXTLINE(bugprone-branch-clone) - different parameter passing semantics
    auto register_handler = [this](const std::string& type, auto&& callback) {
      if constexpr (std::is_pointer_v<WireMessage>) {
        app_->register_callback(type,
            [handler = std::forward<decltype(callback)>(callback)](
                WireMessage wire_msg) mutable { handler(wire_msg); });
      } else {
        app_->register_callback(type,
            [handler = std::forward<decltype(callback)>(callback)](
                const WireMessage& wire_msg) mutable { handler(wire_msg); });
      }
    };

    register_handler("A", [this](auto&& msg) { on_login(msg); });
    register_handler("W", [this](auto&& msg) { on_snapshot(msg); });
    register_handler("X", [this](auto&& msg) { on_subscribe(msg); });
    register_handler("1", [this](auto&& msg) { on_heartbeat(msg); });
    register_handler("y", [this](auto&& msg) { on_instrument_list(msg); });
    register_handler("3", [this](auto&& msg) { on_reject(msg); });
    register_handler("5", [this](auto&& msg) { on_logout(msg); });

    logger_.info("[Constructor] MarketConsumer Created");
  }

  void start() {
    if (!app_->start()) {
      logger_.info("[MarketConsumer] Market Data Start");
    }
  }

  ~MarketConsumer() {
#ifdef ENABLE_WEBSOCKET
    for (auto* buffered : buffered_events_) {
      for (auto* market_data : buffered->data) {
        market_data_pool_->deallocate(market_data);
      }
      market_update_data_pool_->deallocate(buffered);
    }
    buffered_events_.clear();
#endif
    std::cout << "[Destructor] MarketConsumer Destroy\n";
  }

  void stop() { app_->stop(); }

  void on_login(WireMessage msg) {
#ifdef ENABLE_WEBSOCKET
    ProtocolPolicy::handle_login(*app_,
        std::move(msg),
        state_,
        buffered_events_,
        first_buffered_update_id_,
        logger_,
        on_instrument_info_fn_);
#else
    std::deque<MarketUpdateData*> dummy_buffered;
    uint64_t dummy_first_buffered = 0;
    ProtocolPolicy::handle_login(*app_,
        std::move(msg),
        state_,
        dummy_buffered,
        dummy_first_buffered,
        logger_,
        on_instrument_info_fn_);
#endif
  }

  void on_snapshot(const WireMessage& msg) {
    logger_.info("[MarketConsumer]Snapshot making start");

    auto* snapshot_data = market_update_data_pool_->allocate(
        app_->create_snapshot_data_message(msg));

    if (UNLIKELY(snapshot_data == nullptr)) {
      logger_.error(
          "[MarketConsumer] Market update data pool exhausted on snapshot");
#ifdef ENABLE_WEBSOCKET
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
      const std::string snapshot_req = app_->create_snapshot_request_message(
          INI_CONFIG.get("meta", "ticker"),
          INI_CONFIG.get("meta", "level"));
      std::ignore = app_->send(snapshot_req);
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
        static constexpr int kSnapshotRetryDelaySeconds = 10;
        std::this_thread::sleep_for(
            std::chrono::seconds(kSnapshotRetryDelaySeconds));

        const std::string snapshot_req = app_->create_snapshot_request_message(
            INI_CONFIG.get("meta", "ticker"),
            INI_CONFIG.get("meta", "level"));
        std::ignore = app_->send(snapshot_req);
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
    bool first_buffered = true;  // NOLINT(misc-const-correctness)
    constexpr auto kMarketType =
        get_market_type<typename MdApp::ExchangeTraits>();

    for (auto* buffered : buffered_events_) {
      DepthValidationResult validation_result;
      if (first_buffered) {
        validation_result = validate_first_depth_after_snapshot<kMarketType>(
            buffered->start_idx,
            buffered->end_idx,
            update_index_);
        first_buffered = false;
      } else {
        validation_result = validate_continuous_depth(kMarketType,
            buffered->start_idx,
            buffered->end_idx,
            buffered->prev_end_idx,
            update_index_);
      }

      if (validation_result.valid) {
        update_index_ = validation_result.new_update_index;
        on_market_data_fn_(buffered);
      } else {
        logger_.error(
            "[MarketConsumer]Buffered event gap detected! Expected pu:{}, got "
            "pu:{}, start:{}, end:{}",
            update_index_,
            buffered->prev_end_idx,
            buffered->start_idx,
            buffered->end_idx);
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
    first_depth_after_snapshot_ = true;
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

  void on_subscribe(WireMessage msg) {
#ifdef ENABLE_WEBSOCKET
    ProtocolPolicy::handle_subscribe(*app_,
        msg,
        state_,
        buffered_events_,
        first_buffered_update_id_,
        update_index_,
        first_depth_after_snapshot_,
        on_market_data_fn_,
        market_update_data_pool_,
        market_data_pool_,
        logger_,
        [this]() { recover_from_gap(); });
#else
    std::deque<MarketUpdateData*> dummy_buffered;
    uint64_t dummy_first_buffered = 0;
    bool dummy_first_depth = false;
    ProtocolPolicy::handle_subscribe(*app_,
        msg,
        state_,
        dummy_buffered,
        dummy_first_buffered,
        update_index_,
        dummy_first_depth,
        on_market_data_fn_,
        market_update_data_pool_,
        market_data_pool_,
        logger_,
        [this]() { resubscribe(); });
#endif
  }

  void on_reject(const WireMessage& msg) const {
    const auto rejected_message = app_->create_reject_message(msg);
    logger_.error("[MarketConsumer][Message] {}", rejected_message.toString());
    if (rejected_message.session_reject_reason == "A") {
      app_->stop();
    }
  }

  void on_logout(const WireMessage& /*msg*/) const {
    logger_.info("[MarketConsumer][Message] logout");
  }

  void on_instrument_list(const WireMessage& msg) const {
    const InstrumentInfo instrument_message =
        app_->create_instrument_list_message(msg);
    logger_.info("[MarketConsumer][Message] on_instrument_list :{}",
        instrument_message.toString());
    on_instrument_info_fn_(instrument_message);
  }

  void on_heartbeat(const WireMessage& msg) const {
    auto message = app_->create_heartbeat_message(msg);
    if (UNLIKELY(!app_->send(message))) {
      logger_.error("[MarketConsumer][Message] failed to send heartbeat");
    }
  }

  // Recovery methods (implementation in CRTP base)
  void recover_from_gap() { this->recover_from_gap_impl(); }
  void erase_buffer_lower_than_snapshot(uint64_t snapshot_update_id) {
    this->erase_buffer_lower_than_snapshot_impl(snapshot_update_id);
  }
  void resubscribe() { this->resubscribe_impl(); }

  MdApp& app() { return *app_; }
  [[nodiscard]] const MdApp& app() const { return *app_; }

 private:
  common::MemoryPool<MarketUpdateData>* market_update_data_pool_;
  common::MemoryPool<MarketData>* market_data_pool_;
  const common::Logger::Producer& logger_;
  std::function<bool(MarketUpdateData*)> on_market_data_fn_;
  std::function<void(const InstrumentInfo&)> on_instrument_info_fn_;
  std::unique_ptr<MdApp> app_;
  uint64_t update_index_ = 0ULL;

  StreamState state_{StreamState::kAwaitingSnapshot};
  int retry_count_{0};
#ifdef ENABLE_WEBSOCKET
  std::deque<MarketUpdateData*> buffered_events_;
  uint64_t first_buffered_update_id_{0};
  bool first_depth_after_snapshot_{false};
#else
  std::atomic<uint64_t> generation_{0};
  std::atomic<uint64_t> current_generation_{0};
#endif
};

}  // namespace trading

#include "market_consumer_recovery.hpp"

#endif  // MARKET_CONSUMER_HPP
