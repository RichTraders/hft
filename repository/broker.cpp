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
#include "scope_exit.h"

using common::FileSink;
using common::LogLevel;

Broker::Broker()
    : market_update_data_pool_(
          std::make_unique<common::MemoryPool<MarketUpdateData>>(
              kMarketUpdateDataMemoryPoolSize)),
      market_data_pool_(
          std::make_unique<common::MemoryPool<MarketData>>(kMemoryPoolSize)),
      log_(std::make_unique<common::Logger>()),
      log_producer_(log_->make_producer()) {

#ifdef TEST_NET
  INI_CONFIG.load("resources/test_config.ini");
#else
  INI_CONFIG.load("resources/config.ini");
#endif

  app_ = std::make_unique<core::FixMarketDataApp>(
      "BMDWATCH", "SPOT", log_.get(), market_data_pool_.get());

  log_->setLevel(LogLevel::kInfo);
  log_->clearSink();
  log_->addSink(std::make_unique<common::FileSink>(
      "repository", INI_CONFIG.get_int("log", "size")));
  log_->addSink(std::make_unique<common::ConsoleSink>());

  app_->register_callback(
      "A", [this](auto&& msg) { on_login(std::forward<decltype(msg)>(msg)); });
  app_->register_callback("Y", [this](auto&& msg) {
    on_market_request_reject(std::forward<decltype(msg)>(msg));
  });
  app_->register_callback(
      [this](auto&& str_msg, auto&& msg, auto&& event_type) {
        on_subscribe(str_msg, msg, event_type);
      });
  app_->register_callback("1", [this](auto&& msg) {
    on_heartbeat(std::forward<decltype(msg)>(msg));
  });

  app_->start();
}
void Broker::on_login(FIX8::Message*) {
  std::cout << "login successful\n";
  const std::string message = app_->create_market_data_subscription_message(
      "DEPTH_STREAM", INI_CONFIG.get("meta", "level"),
      INI_CONFIG.get("meta", "ticker"), true);
  std::cout << "Market subscription message : " << message << "\n";
  app_->send(message);
}

void Broker::on_market_request_reject(FIX8::Message*) {
  log_producer_.error("Market subscription rejected");
}

void Broker::on_heartbeat(FIX8::Message* msg) {
  auto message = app_->create_heartbeat_message(msg);
  app_->send(message);
}

#ifdef LIGHT_LOGGER
void Broker::on_subscribe(const std::string& str_msg, FIX8::Message*,
                          const std::string&) {
#else
void Broker::on_subscribe(const std::string& str_msg, FIX8::Message* msg,
                          const std::string& event_type) {
#endif
  log_producer_.info(str_msg);
#ifndef LIGHT_LOGGER
  if (event_type != "X") {
    return;
  }
  subscribed_ = true;

  MarketUpdateData* data =
      market_update_data_pool_->allocate(app_->create_market_data_message(msg));

  if (!data) {
    log_producer_.error(
        "[Error] Failed to allocate market data message, but log is here");
    return;
  }

  auto cleanup = MakeScopeExit([&] {
    for (auto* iter : data->data) {
      if (iter)
        market_data_pool_->deallocate(iter);
    }
    market_update_data_pool_->deallocate(data);
    data = nullptr;
  });

  if (data->type == kNone || (data->type == kMarket &&
                              (data->start_idx != this->update_index_ + 1ULL) &&
                              (this->update_index_ != 0ULL) && subscribed_)) {
    log_producer_.error(std::format(
        "Update index is outdated. current index :{}, new index :{}",
        this->update_index_, data->start_idx));

    // re-subscribe
    {
      const std::string msg_unsub =
          app_->create_market_data_subscription_message(
              "DEPTH_STREAM", INI_CONFIG.get("meta", "level"),
              INI_CONFIG.get("meta", "ticker"), /*subscribe=*/false);
      app_->send(msg_unsub);

      const std::string msg_sub = app_->create_market_data_subscription_message(
          "DEPTH_STREAM", INI_CONFIG.get("meta", "level"),
          INI_CONFIG.get("meta", "ticker"), /*subscribe=*/true);
      app_->send(msg_sub);
      subscribed_ = false;
    }
    this->update_index_ = 0ULL;

    return;
  }

  this->update_index_ = data->end_idx;
#endif
}