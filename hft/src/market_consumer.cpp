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

#include "market_consumer.h"
#include "fix_md_app.h"
#include "trade_engine.h"

namespace trading {
MarketConsumer::MarketConsumer(
    common::Logger* logger, TradeEngine* trade_engine,
    common::MemoryPool<MarketUpdateData>* market_update_data_pool,
    common::MemoryPool<MarketData>* market_data_pool,
    const Authorization& authorization)
    : market_update_data_pool_(market_update_data_pool),
      market_data_pool_(market_data_pool),
      logger_(logger),
      trade_engine_(trade_engine),
      app_(std::make_unique<core::FixMarketDataApp>(
          authorization, "BMDWATCH", "SPOT", logger_, market_data_pool_)) {

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
  app_->register_callback(
      "3", [this](auto&& msg) { on_reject(std::forward<decltype(msg)>(msg)); });
  app_->register_callback(
      "5", [this](auto&& msg) { on_logout(std::forward<decltype(msg)>(msg)); });

  app_->start();
}

MarketConsumer::~MarketConsumer() = default;

void MarketConsumer::on_login(FIX8::Message*) const {
  logger_->info("login successful");
  const std::string message = app_->create_market_data_subscription_message(
      "DEPTH_STREAM", "1000", "BTCUSDT");
  app_->send(message);
  logger_->info("sent order message");
}

void MarketConsumer::on_snapshot(FIX8::Message* msg) const {
  auto* snapshot_data = market_update_data_pool_->allocate(
      app_->create_snapshot_data_message(msg));
  trade_engine_->on_market_data_updated(snapshot_data);
}

void MarketConsumer::on_subscribe(FIX8::Message* msg) const {
  auto* data =
      market_update_data_pool_->allocate(app_->create_market_data_message(msg));
  trade_engine_->on_market_data_updated(data);
}

void MarketConsumer::on_reject(FIX8::Message*) const {
  logger_->info("reject data");
}

void MarketConsumer::on_logout(FIX8::Message*) const {
  logger_->info("logout");
}

void MarketConsumer::on_heartbeat(FIX8::Message* msg) const {
  auto message = app_->create_heartbeat_message(msg);
  app_->send(message);
}
}  // namespace trading