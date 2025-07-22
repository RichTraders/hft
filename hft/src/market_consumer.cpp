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
#include "core/NewOroFix44/fix_app.h"
#include "trade_engine.h"

constexpr int kPort = 9000;

MarketConsumer::MarketConsumer()
#ifdef DEBUG
    : app_(std::make_unique<core::FixApp<>>("fix-md.testnet.binance.vision",
#else
    : app_(std::make_unique<core::FixApp<1>>("fix-md.binance.com",
#endif
                                            kPort, "BMDWATCH", "SPOT")),
      trade_engine_(std::make_unique<TradeEngine>()) {
  //app(core::FixApp("fix-md.binance.com", 9000)) {

  app_->register_callback(
      "A", [this](auto&& msg) { on_login(std::forward<decltype(msg)>(msg)); });
  app_->register_callback("W", [this](auto&& msg) {
    on_subscribe(std::forward<decltype(msg)>(msg));
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

MarketConsumer::~MarketConsumer() {
  app_->stop();
}

void MarketConsumer::on_login(FIX8::Message*) {
  std::cout << "login successful\n";
  const std::string message =
      app_->create_subscription_message("DEPTH_STREAM", "5000", "BTCUSDT");
  std::cout << "snapshot : " << message << "\n";
  app_->send(message);
}

void MarketConsumer::on_subscribe(FIX8::Message*) {}

void MarketConsumer::on_reject(FIX8::Message*) {}

void MarketConsumer::on_logout(FIX8::Message*) {}

void MarketConsumer::on_heartbeat(FIX8::Message*) {
  auto message = app_->create_heartbeat_message();
  app_->send(message);
}