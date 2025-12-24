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

#ifndef MARKET_CONSUMER_RECOVERY_HPP
#define MARKET_CONSUMER_RECOVERY_HPP

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include "common/ini_config.hpp"
#include "stream_state.h"

namespace trading {

#ifdef ENABLE_WEBSOCKET

template <typename Derived>
class MarketConsumerRecoveryMixin {
 protected:
  void recover_from_gap_impl() {
    auto& self = static_cast<Derived&>(*this);

    if (self.state_ == StreamState::kBuffering) {
      self.logger_.info(
          "[MarketConsumer]Gap detected, but already snapshot buffering mode.");
      return;
    }

    self.logger_.info("[MarketConsumer]Gap detected, entering buffering mode");
    self.state_ = StreamState::kBuffering;
    self.buffered_events_.clear();
    self.first_buffered_update_id_ = 0;

    const std::string snapshot_req = self.app_->create_snapshot_request_message(
        INI_CONFIG.get("meta", "ticker"),
        INI_CONFIG.get("meta", "level"));
    std::ignore = self.app_->send(snapshot_req);

    self.logger_.info("Gap detected, resubscribing");
  }

  void erase_buffer_lower_than_snapshot_impl(
      const uint64_t snapshot_update_id) {
    auto& self = static_cast<Derived&>(*this);

    while (!self.buffered_events_.empty()) {
      if (const auto* event = self.buffered_events_.front();
          event->end_idx <= snapshot_update_id) {
        for (const auto* market_data : event->data)
          self.market_data_pool_->deallocate(market_data);
        self.market_update_data_pool_->deallocate(event);
        self.buffered_events_.pop_front();
      } else {
        break;
      }
    }
  }
};

#else

// FIX Recovery Mixin
template <typename Derived>
class MarketConsumerRecoveryMixin {
 protected:
  void recover_from_gap_impl() {
    auto& self = static_cast<Derived&>(*this);

    self.current_generation_.fetch_add(1, std::memory_order_acq_rel);
    unsubscribe_and_resubscribe(self);
    self.state_ = StreamState::kAwaitingSnapshot;
    self.update_index_ = 0ULL;
  }

  void resubscribe_impl() {
    auto& self = static_cast<Derived&>(*this);

    self.logger_.info("Try resubscribing");
    self.current_generation_.store(
        self.generation_.fetch_add(1, std::memory_order_acq_rel) + 1,
        std::memory_order_release);

    unsubscribe_and_resubscribe(self);

    ++self.generation_;
    self.state_ = StreamState::kAwaitingSnapshot;
    self.update_index_ = 0ULL;
  }

 private:
  void unsubscribe_and_resubscribe(Derived& self) {
    const std::string msg_unsub =
        self.app_->create_market_data_subscription_message("DEPTH_STREAM",
            INI_CONFIG.get("meta", "level"),
            INI_CONFIG.get("meta", "ticker"),
            /*subscribe=*/false);
    self.app_->send(msg_unsub);

    std::this_thread::sleep_for(std::chrono::seconds(5));

    const std::string msg_sub =
        self.app_->create_market_data_subscription_message("DEPTH_STREAM",
            INI_CONFIG.get("meta", "level"),
            INI_CONFIG.get("meta", "ticker"),
            /*subscribe=*/true);
    self.app_->send(msg_sub);
  }
};

#endif

}  // namespace trading

#endif  // MARKET_CONSUMER_RECOVERY_HPP
