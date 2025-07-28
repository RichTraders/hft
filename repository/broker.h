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

#ifndef LOGGER_PREFIX_DISABLED
#define LOGGER_PREFIX_DISABLED
#endif

#ifndef REPOSITORY
#define REPOSITORY
#endif

#include "common/logger.h"
#include "core/NewOroFix44/fix_app.h"
#include "memory_pool.hpp"

class Broker {
 public:
  Broker();

 private:
  static constexpr int kMemoryPoolSize = 1024;

  std::unique_ptr<common::MemoryPool<MarketData>> pool_;
  std::unique_ptr<common::Logger> log_;
  std::unique_ptr<core::FixApp<>> app_;

  void on_login(FIX8::Message*) const;
  void on_heartbeat(FIX8::Message*) const;
  void on_subscribe(const std::string& msg);
};

#endif  //BROKER_H