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

#ifndef BROKER_H
#define BROKER_H

#include "common/logger.h"
#include "core/NewOroFix44/fix_md_app.h"
#include "memory_pool.hpp"

class Broker {
 public:
  Broker();

 private:
  static constexpr int kMemoryPoolSize = 65536;
  static constexpr int kMarketUpdateDataMemoryPoolSize = 1024;

  std::unique_ptr<common::MemoryPool<MarketUpdateData>>
      market_update_data_pool_;
  std::unique_ptr<common::MemoryPool<MarketData>> market_data_pool_;
  std::unique_ptr<common::Logger> log_;
  common::Logger::Producer log_producer_;
  std::unique_ptr<core::FixMarketDataApp> app_;

#ifndef LIGHT_LOGGER
  bool subscribed_ = false;
  uint64_t update_index_ = 0ULL;
#endif

  void on_login(FIX8::Message*);
  void on_market_request_reject(FIX8::Message*);
  void on_heartbeat(FIX8::Message*);
  void on_subscribe(const std::string& str_msg, FIX8::Message* msg,
                    const std::string& event_type);
};

#endif  //BROKER_H