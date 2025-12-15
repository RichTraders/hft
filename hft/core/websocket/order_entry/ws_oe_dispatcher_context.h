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

#ifndef WS_OE_DISPATCHER_CONTEXT_H
#define WS_OE_DISPATCHER_CONTEXT_H

#include "common/logger.h"
#include "ws_order_manager.hpp"
namespace core {
class WsOrderEntryApp;

template <typename ExchangeTraits>
struct WsOeDispatchContext {
  using WireMessage = typename ExchangeTraits::WireMessage;

  const common::Logger::Producer* logger;
  WsOrderManager<ExchangeTraits>* order_manager;
  WsOrderEntryApp* app;

  WsOeDispatchContext()
      : logger(nullptr), order_manager(nullptr), app(nullptr) {}

  WsOeDispatchContext(const common::Logger::Producer* log,
      WsOrderManager<ExchangeTraits>* mgr, WsOrderEntryApp* application)
      : logger(log), order_manager(mgr), app(application) {}
};

}  // namespace core

#endif  // WS_OE_DISPATCHER_CONTEXT_H
