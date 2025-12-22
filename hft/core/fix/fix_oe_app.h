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

#ifndef FIX_OE_APP_H
#define FIX_OE_APP_H
#include "authorization.h"
#include "core/order_entry.h"
#include "core/protocol_concepts.h"
#include "fix_app.h"
#include "fix_oe_core.h"

namespace FIX8 {
class Message;
}

namespace trading {
class ResponseManager;
}

namespace core {

class FixOrderEntryApp : public FixApp<FixOrderEntryApp, "OERead", "OEWrite"> {
 public:
  using WireMessage = FixOeCore::WireMessage;
  using WireExecutionReport = FixOeCore::WireExecutionReport;
  using WireCancelReject = FixOeCore::WireCancelReject;
  using WireMassCancelReport = FixOeCore::WireMassCancelReport;
  using WireReject = FixOeCore::WireReject;

  FixOrderEntryApp(const std::string& sender_comp_id,
      const std::string& target_comp_id,
      const common::Logger::Producer& logger,
      trading::ResponseManager* response_manager);
  ~FixOrderEntryApp();
  std::string create_log_on_message(const std::string& sig_b64,
      const std::string& timestamp);
  std::string create_log_out_message();
  std::string create_heartbeat_message(WireMessage message);
  std::string create_order_message(
      const trading::NewSingleOrderData& order_data);
  std::string create_cancel_order_message(
      const trading::OrderCancelRequest& cancel_request);
  std::string create_cancel_and_reorder_message(
      const trading::OrderCancelAndNewOrderSingle& cancel_and_re_order);
  std::string create_order_all_cancel(
      const trading::OrderMassCancelRequest& all_order_cancel);
  trading::ExecutionReport* create_execution_report_message(
      WireExecutionReport msg);
  trading::OrderCancelReject* create_order_cancel_reject_message(
      WireCancelReject msg);
  trading::OrderMassCancelReport* create_order_mass_cancel_report_message(
      WireMassCancelReport msg);
  trading::OrderReject create_reject_message(WireReject msg);

  WireMessage decode(const std::string& message);

  void post_new_order(const trading::NewSingleOrderData&);
  void post_cancel_order(const trading::OrderCancelRequest&);
  void post_cancel_and_reorder(const trading::OrderCancelAndNewOrderSingle &);
  void post_mass_cancel_order(const trading::OrderMassCancelRequest&);

  [[nodiscard]] bool is_session_ready() const noexcept {
    return session_ready_.load(std::memory_order_acquire);
  }
  void set_session_ready() noexcept {
    session_ready_.store(true, std::memory_order_release);
  }

 private:
  std::atomic<bool> session_ready_{false};
  std::unique_ptr<FixOeCore> fix_oe_core_;
};

// static_assert(core::OrderEntryCore<FixOeCore>,
//     "FixOeCore must satisfy the OrderEntryCore concept");
}  // namespace core

#endif