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
#include "fix_app.h"
#include "fix_oe_core.h"
#include "order_entry.h"
#include "authorization.h"

namespace FIX8 {
class Message;
}

namespace trading {
class ResponseManager;
}

namespace core {

class FixOrderEntryApp : public FixApp<FixOrderEntryApp, "OERead", "OEWrite"> {
public:
  FixOrderEntryApp(const std::string& sender_comp_id,
                   const std::string& target_comp_id, common::Logger* logger, trading::ResponseManager* response_manager)
    : FixApp(AUTHORIZATION.get_od_address(), AUTHORIZATION.get_port(), sender_comp_id,
             target_comp_id, logger){
    fix_oe_core_ = std::make_unique<FixOeCore>(sender_comp_id, target_comp_id,
                                               logger, response_manager);
  }
  std::string create_log_on_message(const std::string& sig_b64,
                                    const std::string& timestamp);
  std::string create_log_out_message();
  std::string create_heartbeat_message(FIX8::Message* message);
  std::string create_order_message(
      const trading::NewSingleOrderData& order_data);
  std::string create_cancel_order_message(
      const trading::OrderCancelRequest& cancel_request);
  std::string create_cancel_and_reorder_message(
      const trading::OrderCancelRequestAndNewOrderSingle& cancel_and_re_order);
  std::string create_order_all_cancel(const trading::OrderMassCancelRequest& all_order_cancel);
  trading::ExecutionReport* create_execution_report_message(
      FIX8::NewOroFix44OE::ExecutionReport* msg);
  trading::OrderCancelReject* create_order_cancel_reject_message(FIX8::NewOroFix44OE::OrderCancelReject* msg);
  trading::OrderMassCancelReport* create_order_mass_cancel_report_message(FIX8::NewOroFix44OE::OrderMassCancelReport* msg);
  FIX8::Message* decode(const std::string& message);

private:
  std::unique_ptr<FixOeCore> fix_oe_core_;
};
} // namespace core