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

#include "market_consumer.h"
#include "fix_md_app.h"
#include "ini_config.hpp"
#include "trade_engine.h"

namespace trading {

template<typename Strategy>
MarketConsumer<Strategy>::MarketConsumer(
    common::Logger* logger, TradeEngine<Strategy>* trade_engine,
    common::MemoryPool<MarketUpdateData>* market_update_data_pool,
    common::MemoryPool<MarketData>* market_data_pool)
    : market_update_data_pool_(market_update_data_pool),
      market_data_pool_(market_data_pool),
      logger_(logger->make_producer()),
      trade_engine_(trade_engine),
      app_(std::make_unique<core::FixMarketDataApp>("BMDWATCH", "SPOT", logger,
                                                    market_data_pool_)) {

  app_->register_callback(
      "A", [this](auto&& msg) { on_login(std::forward<decltype(msg)>(msg)); });
  app_->register_callback("W", [this](auto&& msg) {
    on_snapshot(std::forward<decltype(msg)>(msg));
  });
  app_->register_callback("X", [this](auto&& msg) {
    on_subscribe(std::forward<decltype(msg)>(msg));
  });
  app_->register_callback("1", [this](auto&& msg) {
    on_heartbeat(std::forward<decltype(msg)>(msg));
  });
  app_->register_callback("y", [this](auto&& msg) {
    on_instrument_list(std::forward<decltype(msg)>(msg));
  });
  app_->register_callback(
      "3", [this](auto&& msg) { on_reject(std::forward<decltype(msg)>(msg)); });
  app_->register_callback(
      "5", [this](auto&& msg) { on_logout(std::forward<decltype(msg)>(msg)); });

  app_->start();
  logger_.info("[Constructor] MarketConsumer Created");
}

template<typename Strategy>
MarketConsumer<Strategy>::~MarketConsumer() {
  logger_.info("[Destructor] MarketConsumer Destroy");
}

template<typename Strategy>
void MarketConsumer<Strategy>::stop() {
  app_->stop();
}

template<typename Strategy>
void MarketConsumer<Strategy>::on_login(FIX8::Message*) {
  logger_.info("[Login] Market consumer successful");
  const std::string message = app_->create_market_data_subscription_message(
      "DEPTH_STREAM", INI_CONFIG.get("meta", "level"),
      INI_CONFIG.get("meta", "ticker"), true);

  if (UNLIKELY(!app_->send(message))) {
    logger_.error("[Message] failed to send login");
  }

  const std::string instrument_message =
      app_->request_instrument_list_message(INI_CONFIG.get("meta", "ticker"));
  if (UNLIKELY(!app_->send(instrument_message))) {
    logger_.error("[Message] failed to send instrument list");
  }
}

template<typename Strategy>
void MarketConsumer<Strategy>::on_snapshot(FIX8::Message* msg) {
  logger_.info("Snapshot made");

  auto* snapshot_data = market_update_data_pool_->allocate(
      app_->create_snapshot_data_message(msg));

  if (UNLIKELY(snapshot_data == nullptr)) {
    logger_.error("[Message] failed to create snapshot");
    resubscribe();

    for (auto& market_data : snapshot_data->data) {
      market_data_pool_->deallocate(market_data);
    }
    market_update_data_pool_->deallocate(snapshot_data);
    return;
  }

  state_ = StreamState::kApplyingSnapshot;
  update_index_ = snapshot_data->end_idx;

  if (UNLIKELY(!trade_engine_->on_market_data_updated(snapshot_data))) {
    logger_.error("[Message] failed to send snapshot");
  }

  state_ = StreamState::kRunning;
}

template<typename Strategy>
void MarketConsumer<Strategy>::on_subscribe(FIX8::Message* msg) {
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
    logger_.error(std::format(
        "Update index is outdated. current index :{}, new index :{}",
        this->update_index_, data->start_idx));

    resubscribe();

    for (const auto& market_data : data->data) {
      market_data_pool_->deallocate(market_data);
    }
    market_update_data_pool_->deallocate(data);
    return;
  }

  this->update_index_ = data->end_idx;
  if (UNLIKELY(!trade_engine_->on_market_data_updated(data))) {
    logger_.error("[Message] failed to send subscribe");
  }
}

template<typename Strategy>
void MarketConsumer<Strategy>::on_reject(FIX8::Message* msg) {
  const auto rejected_message = app_->create_reject_message(msg);
  logger_.error(std::format("[Message] {}", rejected_message.toString()));
  if (rejected_message.session_reject_reason == "A") {
    app_->stop();
  }
}

template<typename Strategy>
void MarketConsumer<Strategy>::on_logout(FIX8::Message*) {
  logger_.info("[Message] logout");
}

template<typename Strategy>
void MarketConsumer<Strategy>::on_instrument_list(FIX8::Message* msg) {
  const InstrumentInfo instrument_message =
      app_->create_instrument_list_message(msg);
  logger_.info(std::format("[Message] on_instrument_list :{}",
                           instrument_message.toString()));
  trade_engine_->on_instrument_info(instrument_message);
}

template<typename Strategy>
void MarketConsumer<Strategy>::on_heartbeat(FIX8::Message* msg) {
  auto message = app_->create_heartbeat_message(msg);
  if (UNLIKELY(!app_->send(message))) {
    logger_.error("[Message] failed to send heartbeat");
  }
}

template<typename Strategy>
void MarketConsumer<Strategy>::resubscribe() {
  logger_.info("Try resubscribing");
  current_generation_.store(
      generation_.fetch_add(1, std::memory_order_acq_rel) + 1,
      std::memory_order_release);

  const std::string msg_unsub = app_->create_market_data_subscription_message(
      "DEPTH_STREAM", INI_CONFIG.get("meta", "level"),
      INI_CONFIG.get("meta", "ticker"), /*subscribe=*/false);
  app_->send(msg_unsub);

  //TODO(JB) I'm not sure whether it's a good way.
  //std::this_thread::sleep_for(std::chrono::seconds(5));

  const std::string msg_sub = app_->create_market_data_subscription_message(
      "DEPTH_STREAM", INI_CONFIG.get("meta", "level"),
      INI_CONFIG.get("meta", "ticker"), /*subscribe=*/true);
  app_->send(msg_sub);

  ++generation_;
  state_ = StreamState::kAwaitingSnapshot;
  update_index_ = 0ULL;
}

}  // namespace trading

#endif  // MARKET_CONSUMER_TPP
