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

#ifndef HFT_DIABLED_LISTEN_KEY_MANAGER_H
#define HFT_DIABLED_LISTEN_KEY_MANAGER_H
#include <common/logger.h>

struct DisabledListenKeyManager {
  explicit DisabledListenKeyManager(const common::Logger::Producer&) {}
  constexpr explicit operator bool() const noexcept { return false; }

  const DisabledListenKeyManager* operator->() const { return this; }

  void reset() const {}
  void stop_keepalive() const {}
  void release_listen_key() const {}
  [[nodiscard]] bool acquire_listen_key() const { return true; }
  void start_keepalive() const {}
};

#endif  //HFT_DIABLED_LISTEN_KEY_MANAGER_H
