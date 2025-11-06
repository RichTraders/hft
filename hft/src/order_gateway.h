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
#pragma once

#include "gateway/gateway_interface.h"
#include "logger.h"
#include "order_entry.h"

namespace trading {
class TradeEngine;

/**
 * @brief Order gateway that delegates to a pluggable IGateway implementation
 *
 * This class acts as a facade that injects a gateway implementation (FIX, WebSocket,
 * or Test) for order execution, enabling runtime selection and testability.
 */
class OrderGateway {
 public:
  OrderGateway(common::Logger* logger, IGateway* gateway);
  ~OrderGateway();

  void init_trade_engine(TradeEngine* trade_engine);
  void stop() const;

  void order_request(const RequestCommon& request);

 private:
  common::Logger::Producer logger_;
  TradeEngine* trade_engine_;
  IGateway* gateway_;
};
}  // namespace trading