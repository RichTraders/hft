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

  IniConfig config;
#ifdef TEST_NET
  config.load("resources/test_config.ini");
#else
  config.load("resources/config.ini");
#endif

  const Authorization authorization{
      .md_address = config.get("auth", "md_address"),
      .oe_address = config.get("auth", "oe_address"),
      .port = config.get_int("auth", "port"),
      .api_key = config.get("auth", "api_key"),
      .pem_file_path = config.get("auth", "pem_file_path"),
      .private_password = config.get("auth", "private_password")};

  app_ = std::make_unique<core::FixMarketDataApp>(
      authorization, "BMDWATCH", "SPOT", log_.get(), pool_.get());

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
void Broker::on_login(FIX8::Message*) {
  std::cout << "login successful\n";
  const std::string message = app_->create_market_data_subscription_message(
      "DEPTH_STREAM", "5000", "BTCUSDT");
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