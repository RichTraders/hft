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

#include "fix_oe_app.h"
#include "logger.h"
#include "memory_pool.hpp"

namespace FIX8 {  // NOLINT(readability-identifier-naming)
class Message;
}

namespace trading {
class TradeEngine;

class OrderGateway {
 public:
  OrderGateway(common::Logger* logger, TradeEngine* trade_engine,
               common::MemoryPool<OrderData>* order_data_pool);
  ~OrderGateway();

  void on_login(FIX8::Message*);
  void on_execution_report(FIX8::Message* msg);
  void on_order_cancel_reject(FIX8::Message* msg);
  void on_order_mass_cancel_report(FIX8::Message* msg);
  void on_order_mass_status_response(FIX8::Message* msg);
  void on_logout(FIX8::Message*);
  void on_heartbeat(FIX8::Message* msg);

 private:
  common::Logger* logger_;
  TradeEngine* trade_engine_;
  common::MemoryPool<OrderData>* order_data_pool_;
  std::unique_ptr<core::FixOrderEntryApp> app_;
};
}  // namespace trading
