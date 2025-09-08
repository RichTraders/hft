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
#include "ini_config.hpp"
#include "pid_notifier.hpp"
#include "trade_engine.h"

namespace trading {
MarketConsumer::MarketConsumer(
    common::Logger* logger, TradeEngine* trade_engine,
    common::MemoryPool<MarketUpdateData>* market_update_data_pool,
    common::MemoryPool<MarketData>* market_data_pool)
    : market_update_data_pool_(market_update_data_pool),
      market_data_pool_(market_data_pool),
      logger_(logger),
      trade_engine_(trade_engine),
      app_(std::make_unique<core::FixMarketDataApp>("BMDWATCH", "SPOT", logger_,
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
  logger_->info("[Constructor] MarketConsumer Created");
}

MarketConsumer::~MarketConsumer() {
  logger_->info("[Destructor] MarketConsumer Destory");
}

void MarketConsumer::stop() {
  app_->stop();
}

void MarketConsumer::on_login(FIX8::Message*) const {
  logger_->info("[Login] Market consumer successful");
  const std::string message = app_->create_market_data_subscription_message(
      "DEPTH_STREAM", INI_CONFIG.get("meta", "level"),
      INI_CONFIG.get("meta", "ticker"));

  if (UNLIKELY(!app_->send(message))) {
    logger_->error("[Message] failed to send login");
  }

  const std::string instrument_message =
      app_->request_instrument_list_message();
  if (UNLIKELY(!app_->send(instrument_message))) {
    logger_->error("[Message] failed to send instrument list");
  }
}

void MarketConsumer::on_snapshot(FIX8::Message* msg) const {
  auto* snapshot_data = market_update_data_pool_->allocate(
      app_->create_snapshot_data_message(msg));

  if (UNLIKELY(!trade_engine_->on_market_data_updated(snapshot_data))) {
    logger_->error("[Message] failed to send snapshot");
  }
}

void MarketConsumer::on_subscribe(FIX8::Message* msg) const {
  auto* data =
      market_update_data_pool_->allocate(app_->create_market_data_message(msg));

  if (UNLIKELY(data == nullptr)) {
    const PidNotifier notifier("/run/pidwatch/pid_watch.fifo");
    const bool result = notifier.notify_now();
    std::cout
        << "[CRITICAL] market update data pool is full and send pid notifier "
        << result << "\n";
#ifdef NDEBUG
    app_->stop();
    exit(1);
    return;
#endif
  }
  if (UNLIKELY(!trade_engine_->on_market_data_updated(data))) {
    logger_->error("[Message] failed to send subscribe");
  }
}

void MarketConsumer::on_reject(FIX8::Message* msg) const {
  logger_->info("[Message] reject data");
  logger_->error(
      std::format("[Message] {}", app_->create_reject_message(msg).toString()));
}

void MarketConsumer::on_logout(FIX8::Message*) const {
  logger_->info("[Message] logout");
}

void MarketConsumer::on_instrument_list(FIX8::Message* msg) const {
  logger_->info("[Message] on_instrument_list");
  const InstrumentInfo instrument_message =
      app_->create_instrument_list_message(msg);
  logger_->info(std::format("[Message] :{}", instrument_message.toString()));
}

void MarketConsumer::on_heartbeat(FIX8::Message* msg) const {
  auto message = app_->create_heartbeat_message(msg);
  if (UNLIKELY(!app_->send(message))) {
    logger_->error("[Message] failed to send heartbeat");
  }
}
}  // namespace trading