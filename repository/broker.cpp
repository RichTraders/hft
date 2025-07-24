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
#include "common/logger.h"

constexpr int kPort = 9000;
constexpr int kKilo = 1024;
constexpr int kThirty = 30;

using common::FileSink;
using common::LogLevel;
using core::FixApp;

Broker::Broker()
    : pool_(std::make_unique<common::MemoryPool<MarketData>>(kMemoryPoolSize)),
      log_(std::make_unique<common::Logger>()),
#ifdef DEBUG
      app_(std::make_unique<FixApp<>>("fix-md.testnet.binance.vision",
#else
      app_(std::make_unique<FixApp<>>("fix-md.binance.com",
#endif
                                      kPort, "BMDWATCH", "SPOT", log_.get(),
                                      pool_.get())) {
  log_->setLevel(LogLevel::kInfo);
  log_->clearSink();
  log_->addSink(
      std::make_unique<FileSink>("repository", kKilo * kKilo * kThirty));

  app_->register_callback(
      "A", [this](auto&& msg) { on_login(std::forward<decltype(msg)>(msg)); });
  app_->register_callback([this](auto&& msg) { on_subscribe(msg); });
  app_->register_callback("1", [this](auto&& msg) {
    on_heartbeat(std::forward<decltype(msg)>(msg));
  });

  app_->start();
}

void Broker::on_login(FIX8::Message*) const {
  std::cout << "login successful\n";
  const std::string message =
      app_->create_subscription_message("DEPTH_STREAM", "5000", "BTCUSDT");
  std::cout << "snapshot : " << message << "\n";
  app_->send(message);
}

void Broker::on_heartbeat(FIX8::Message* msg) const {
  auto message = app_->create_heartbeat_message(msg);
  app_->send(message);
}

void Broker::on_subscribe(const std::string& msg) {
  log_->info(msg);
}