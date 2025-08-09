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
#include "order_entry.h"

namespace FIX8::NewOroFix44OE {
class ExecutionReport;
class OrderCancelReject;
class OrderMassCancelReport;
}  // namespace FIX8::NewOroFix44OE

namespace FIX8 {  // NOLINT(readability-identifier-naming)
class Message;
}

namespace trading {
class TradeEngine;
class ResponseManager;

class OrderGateway {
 public:
  OrderGateway(const Authorization& authorization, common::Logger* logger,
               ResponseManager* response_manager);
  ~OrderGateway();

  void init_trade_engine(TradeEngine* trade_engine);

  void on_login(FIX8::Message*);
  void on_execution_report(FIX8::NewOroFix44OE::ExecutionReport* msg);
  void on_order_cancel_reject(FIX8::NewOroFix44OE::OrderCancelReject* msg);
  void on_order_mass_cancel_report(
      FIX8::NewOroFix44OE::OrderMassCancelReport* msg);
  void on_order_mass_status_response(FIX8::Message* msg);
  void on_logout(FIX8::Message*);
  void on_heartbeat(FIX8::Message* msg);
  void order_request(const RequestCommon& request);

 private:
  void write_loop();
  void new_single_order_data(const RequestCommon& request);
  void order_cancel_request(const RequestCommon& request);
  void order_cancel_request_and_new_order_single(const RequestCommon& request);
  void order_mass_cancel_request(const RequestCommon& request);

  common::Logger* logger_;
  TradeEngine* trade_engine_;
  std::unique_ptr<common::SPSCQueue<RequestCommon>> order_queue_;
  common::Thread<common::NormalTag> write_thread_;
  bool thread_running_{true};

  std::unique_ptr<core::FixOrderEntryApp> app_;
};
}  // namespace trading