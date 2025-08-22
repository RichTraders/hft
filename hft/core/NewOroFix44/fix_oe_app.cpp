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

#include "fix_oe_app.h"

namespace core {

std::string FixOrderEntryApp::create_log_on_message(
    const std::string& sig_b64, const std::string& timestamp) {
  return fix_oe_core_->create_log_on_message(sig_b64, timestamp);
}

std::string FixOrderEntryApp::create_log_out_message() {
  return fix_oe_core_->create_log_out_message();
}

std::string FixOrderEntryApp::create_heartbeat_message(FIX8::Message* message) {
  return fix_oe_core_->create_heartbeat_message(message);
}

std::string FixOrderEntryApp::create_order_message(
    const trading::NewSingleOrderData& order_data) {
  return fix_oe_core_->create_order_message(order_data);
}

std::string FixOrderEntryApp::create_cancel_order_message(
    const trading::OrderCancelRequest& cancel_request) {
  return fix_oe_core_->create_cancel_order_message(cancel_request);
}

std::string FixOrderEntryApp::create_cancel_and_reorder_message(
    const trading::OrderCancelRequestAndNewOrderSingle&
    cancel_and_re_order) {
  return fix_oe_core_->create_cancel_and_reorder_message(
      cancel_and_re_order);
}

std::string FixOrderEntryApp::create_order_all_cancel(const trading::OrderMassCancelRequest& all_order_cancel) {
  return fix_oe_core_->create_order_all_cancel(
      all_order_cancel);
}

trading::ExecutionReport* FixOrderEntryApp::create_execution_report_message(
    FIX8::NewOroFix44OE::ExecutionReport* msg) {
  return fix_oe_core_->create_excution_report_message(msg);
}

trading::OrderCancelReject* FixOrderEntryApp::create_order_cancel_reject_message(FIX8::NewOroFix44OE::OrderCancelReject* msg) {
  return fix_oe_core_->create_order_cancel_reject_message(msg);
}

trading::OrderMassCancelReport*
FixOrderEntryApp::create_order_mass_cancel_report_message(
    FIX8::NewOroFix44OE::OrderMassCancelReport* msg) {
  return fix_oe_core_->create_order_mass_cancel_report_message(msg);
}

trading::OrderReject FixOrderEntryApp::create_reject_message(
    FIX8::NewOroFix44OE::Reject* msg) {
  return fix_oe_core_->create_reject_message(msg);
}

FIX8::Message* FixOrderEntryApp::decode(const std::string& message) {
  return fix_oe_core_->decode(message);
}
}