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

#include "gateway_interface.h"
#include "common/logger.h"
#include "core/NewOroFix44/fix_oe_app.h"
#include "core/NewOroFix44/order_entry.h"

namespace FIX8::NewOroFix44OE {
class ExecutionReport;
class OrderCancelReject;
class OrderMassCancelReport;
class Reject;
}  // namespace FIX8::NewOroFix44OE

namespace FIX8 {
class Message;
}

namespace trading {
class ResponseManager;
class TradeEngine;

/**
 * @brief FIX protocol gateway implementation
 *
 * This gateway wraps the FixOrderEntryApp and provides order execution
 * functionality via the FIX protocol over TLS.
 */
class FixGateway : public IGateway {
 public:
  FixGateway(const std::string& sender_comp_id, const std::string& target_comp_id,
             common::Logger* logger, ResponseManager* response_manager,
             TradeEngine* trade_engine);
  ~FixGateway() override;

  void send_order(const RequestCommon& request) override;
  void stop() override;

 private:
  void new_single_order_data(const RequestCommon& request);
  void order_cancel_request(const RequestCommon& request);
  void order_cancel_request_and_new_order_single(const RequestCommon& request);
  void order_mass_cancel_request(const RequestCommon& request);

  // FIX message callbacks
  void on_login(FIX8::Message* msg);
  void on_execution_report(FIX8::NewOroFix44OE::ExecutionReport* msg);
  void on_order_cancel_reject(FIX8::NewOroFix44OE::OrderCancelReject* msg);
  void on_order_mass_cancel_report(FIX8::NewOroFix44OE::OrderMassCancelReport* msg);
  void on_rejected(FIX8::NewOroFix44OE::Reject* msg);
  void on_logout(FIX8::Message* msg);
  void on_heartbeat(FIX8::Message* msg);

  common::Logger::Producer logger_;
  TradeEngine* trade_engine_;
  std::unique_ptr<core::FixOrderEntryApp> app_;
};

}  // namespace trading
