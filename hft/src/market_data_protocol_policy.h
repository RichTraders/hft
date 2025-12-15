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

#ifndef MARKET_DATA_PROTOCOL_POLICY_H
#define MARKET_DATA_PROTOCOL_POLICY_H

#include "depth_validator.h"
#include "ini_config.hpp"
#include "market_data.h"
#include "stream_state.h"

namespace trading {

struct WebSocketMarketDataPolicy {
  template <typename App, typename Logger, typename OnInstrumentInfoFn>
  static void handle_login(App& app, typename App::WireMessage /*msg*/,
      StreamState& state, std::deque<MarketUpdateData*>& buffered_events,
      uint64_t& first_buffered_update_id, Logger& logger,
      OnInstrumentInfoFn& on_instrument_info_fn) {
    logger.info("[MarketConsumer][Login] Market consumer successful");

    const std::string message =
        app.create_snapshot_request_message(INI_CONFIG.get("meta", "ticker"),
            INI_CONFIG.get("meta", "level"));

    if (UNLIKELY(!app.send(message))) {
      logger.error("[MarketConsumer][Message] failed to send login");
    }
    state = StreamState::kBuffering;
    buffered_events.clear();
    first_buffered_update_id = 0;

    if constexpr (App::ExchangeTraits::uses_http_exchange_info()) {
      std::thread([&app,
                      &on_instrument_info_fn,
                      &logger,
                      symbol = INI_CONFIG.get("meta", "ticker")]() {
        auto instrument_info = app.fetch_instrument_info_http(symbol);
        if (instrument_info) {
          on_instrument_info_fn(*instrument_info);
        } else {
          logger.error(
              "[MarketConsumer][Message] failed to fetch instrument info via "
              "HTTP");
        }
      }).detach();
    } else {
      const std::string instrument_message =
          app.request_instrument_list_message(INI_CONFIG.get("meta", "ticker"));
      if (UNLIKELY(!app.send(instrument_message))) {
        logger.error(
            "[MarketConsumer][Message] failed to send instrument list");
      }
    }
  }

  template <typename App, typename Logger, typename OnMarketDataFn,
      typename MarketUpdateDataPool, typename MarketDataPool>
  static void handle_subscribe(App& app, typename App::WireMessage msg,
      StreamState state, std::deque<MarketUpdateData*>& buffered_events,
      uint64_t& first_buffered_update_id, uint64_t& update_index,
      bool& first_depth_after_snapshot, OnMarketDataFn& on_market_data_fn,
      MarketUpdateDataPool* market_update_data_pool,
      MarketDataPool* market_data_pool, Logger& logger, auto recover_fn) {
    auto* data =
        market_update_data_pool->allocate(app.create_market_data_message(msg));

    if (state == StreamState::kBuffering) {
      if (data->type == kTrade) {
        for (auto* market_data : data->data)
          market_data_pool->deallocate(market_data);
        market_update_data_pool->deallocate(data);
        return;
      }

      if (first_buffered_update_id == 0) {
        first_buffered_update_id = data->start_idx;
      }

      // Limit buffer size to prevent memory exhaustion
      static constexpr size_t kMaxBufferedEvents = 10;
      if (buffered_events.size() >= kMaxBufferedEvents) {
        const auto* oldest = buffered_events.front();
        for (const auto* market_data : oldest->data)
          market_data_pool->deallocate(market_data);
        market_update_data_pool->deallocate(oldest);
        buffered_events.pop_front();

        if (!buffered_events.empty()) {
          first_buffered_update_id = buffered_events.front()->start_idx;
        }
      }

      buffered_events.push_back(data);
      return;
    }

    // Skip gap check for trade events (they don't have sequence numbers)
    if (data->type != kTrade) {
      logger.trace("current update index:{}, data start :{}, data end:{}",
          update_index,
          data->start_idx,
          data->end_idx);

      constexpr auto kMarketType =
          get_market_type<typename App::ExchangeTraits>();
      DepthValidationResult validation_result;
      if (first_depth_after_snapshot) {
        validation_result =
            validate_first_depth_after_snapshot<kMarketType>(data->start_idx,
                data->end_idx,
                update_index);
        first_depth_after_snapshot = false;
      } else {
        validation_result = validate_continuous_depth(kMarketType,
            data->start_idx,
            data->end_idx,
            data->prev_end_idx,
            update_index);
      }

      if (!validation_result.valid) {
        logger.error("Gap detected: expected {}, got start:{}, end:{}",
            update_index + 1,
            data->start_idx,
            data->end_idx);
        recover_fn();
        for (auto* market_data : data->data)
          market_data_pool->deallocate(market_data);
        market_update_data_pool->deallocate(data);
        return;
      }
      update_index = validation_result.new_update_index;
    }

    on_market_data_fn(data);
  }
};

struct FixMarketDataPolicy {
  template <typename App, typename Logger, typename OnInstrumentInfoFn>
  static void handle_login(App& app, typename App::WireMessage /*msg*/,
      StreamState& /*state*/,
      std::deque<MarketUpdateData*>& /*buffered_events*/,
      uint64_t& /*first_buffered_update_id*/, Logger& logger,
      OnInstrumentInfoFn& on_instrument_info_fn) {
    logger.info("[MarketConsumer][Login] Market consumer successful");

    const std::string message =
        app.create_market_data_subscription_message("DEPTH_STREAM",
            INI_CONFIG.get("meta", "level"),
            INI_CONFIG.get("meta", "ticker"),
            true);

    if (UNLIKELY(!app.send(message))) {
      logger.error("[MarketConsumer][Message] failed to send login");
    }

    if constexpr (App::ExchangeTraits::uses_http_exchange_info()) {
      std::thread([&app,
                      &on_instrument_info_fn,
                      &logger,
                      symbol = INI_CONFIG.get("meta", "ticker")]() {
        auto instrument_info = app.fetch_instrument_info_http(symbol);
        if (instrument_info) {
          on_instrument_info_fn(*instrument_info);
        } else {
          logger.error(
              "[MarketConsumer][Message] failed to fetch instrument info via "
              "HTTP");
        }
      }).detach();
    } else {
      const std::string instrument_message =
          app.request_instrument_list_message(INI_CONFIG.get("meta", "ticker"));
      if (UNLIKELY(!app.send(instrument_message))) {
        logger.error(
            "[MarketConsumer][Message] failed to send instrument list");
      }
    }
  }

  template <typename App, typename Logger, typename OnMarketDataFn,
      typename MarketUpdateDataPool, typename MarketDataPool>
  static void handle_subscribe(App& app, typename App::WireMessage msg,
      StreamState state, std::deque<MarketUpdateData*>& /*buffered_events*/,
      uint64_t& /*first_buffered_update_id*/, uint64_t& update_index,
      bool& /*first_depth_after_snapshot*/, OnMarketDataFn& on_market_data_fn,
      MarketUpdateDataPool* market_update_data_pool,
      MarketDataPool* market_data_pool, Logger& logger, auto resubscribe_fn) {
    auto* data =
        market_update_data_pool->allocate(app.create_market_data_message(msg));

    if (UNLIKELY(data == nullptr)) {
      logger.error(
          "[Error] Failed to allocate market data message, but log is here");
#ifdef NDEBUG
      app.stop();
      exit(1);
#endif
      return;
    }

    if (UNLIKELY(state == StreamState::kAwaitingSnapshot)) {
      logger.info("Waiting for making snapshot");
      return;
    }

    if (UNLIKELY(
            (data->type == kNone) ||
            (data->type == kMarket && data->start_idx != update_index + 1 &&
                update_index != 0ULL))) {
      logger.error("Update index is outdated. current index :{}, new index :{}",
          update_index,
          data->start_idx);

      resubscribe_fn();

      for (const auto& market_data : data->data) {
        market_data_pool->deallocate(market_data);
      }
      market_update_data_pool->deallocate(data);
      return;
    }

    update_index = data->end_idx;
    if (UNLIKELY(!on_market_data_fn(data))) {
      logger.error("[Message] failed to send subscribe");
    }
  }
};

template <typename AppType>
struct MarketDataProtocolPolicySelector {
  using type = WebSocketMarketDataPolicy;
};

}  // namespace trading

#endif  // MARKET_DATA_PROTOCOL_POLICY_H
