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
#include "fix_md_app.h"
#include "ini_config.hpp"

constexpr int kKilo = 1024;
constexpr int kThirty = 30;

using common::FileSink;
using common::LogLevel;

Broker::Broker()
    : pool_(std::make_unique<common::MemoryPool<MarketData>>(kMemoryPoolSize)),
      log_(std::make_unique<common::Logger>()) {

#ifdef TEST_NET
  INI_CONFIG.load("resources/test_config.ini");
#else
  INI_CONFIG.load("resources/config.ini");
#endif

  app_ = std::make_unique<core::FixMarketDataApp>("BMDWATCH", "SPOT",
                                                  log_.get(), pool_.get());

  log_->setLevel(LogLevel::kInfo);
  log_->clearSink();
  log_->addSink(
      std::make_unique<FileSink>("repository", kKilo * kKilo * kThirty));
  log_->addSink(std::make_unique<common::ConsoleSink>());

  app_->register_callback(
      "A", [this](auto&& msg) { on_login(std::forward<decltype(msg)>(msg)); });
  app_->register_callback([this](auto&& msg) { on_subscribe(msg); });
  app_->register_callback("1", [this](auto&& msg) {
    on_heartbeat(std::forward<decltype(msg)>(msg));
  });

  app_->start();
}
void Broker::on_login(FIX8::Message*) {
  std::cout << "login successful\n";
  const std::string message = app_->create_market_data_subscription_message(
      "DEPTH_STREAM", INI_CONFIG.get("meta", "level"),
      INI_CONFIG.get("meta", "ticker"));
  std::cout << "snapshot : " << message << "\n";
  app_->send(message);
}

void Broker::on_heartbeat(FIX8::Message* msg) {
  auto message = app_->create_heartbeat_message(msg);
  app_->send(message);
}

void Broker::on_subscribe(const std::string& msg) {
  log_->info(msg);
}