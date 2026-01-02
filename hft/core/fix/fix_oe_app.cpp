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

#include <memory>
#include <string>

#include "fix_oe_app.h"

namespace core {

FixOrderEntryApp::FixOrderEntryApp(const std::string& sender_comp_id,
    const std::string& target_comp_id,
    const common::Logger::Producer& logger,
    trading::ResponseManager* response_manager)
    : FixApp(AUTHORIZATION.get_od_address(), AUTHORIZATION.get_port(),
          sender_comp_id, target_comp_id, logger) {
  fix_oe_core_ = std::make_unique<FixOeCore>(sender_comp_id,
      target_comp_id,
      logger,
      response_manager);
}
FixOrderEntryApp::~FixOrderEntryApp() {
  this->prepare_stop_after_logout();
  this->send(create_log_out_message());
  this->wait_logout_and_halt_io();
}
std::string FixOrderEntryApp::create_log_on_message(const std::string& sig_b64,
    const std::string& timestamp) {
  return fix_oe_core_->create_log_on_message(sig_b64, timestamp);
}

std::string FixOrderEntryApp::create_log_out_message() {
  return fix_oe_core_->create_log_out_message();
}

std::string FixOrderEntryApp::create_heartbeat_message(WireMessage message) {
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
    const trading::OrderCancelAndNewOrderSingle& cancel_and_re_order) {
  return fix_oe_core_->create_cancel_and_reorder_message(cancel_and_re_order);
}

std::string FixOrderEntryApp::create_order_all_cancel(
    const trading::OrderMassCancelRequest& all_order_cancel) {
  return fix_oe_core_->create_order_all_cancel(all_order_cancel);
}

trading::ExecutionReport* FixOrderEntryApp::create_execution_report_message(
    WireExecutionReport msg) {
  return fix_oe_core_->create_execution_report_message(msg);
}

trading::OrderCancelReject*
FixOrderEntryApp::create_order_cancel_reject_message(WireCancelReject msg) {
  return fix_oe_core_->create_order_cancel_reject_message(msg);
}

trading::OrderMassCancelReport*
FixOrderEntryApp::create_order_mass_cancel_report_message(
    WireMassCancelReport msg) {
  return fix_oe_core_->create_order_mass_cancel_report_message(msg);
}

trading::OrderReject FixOrderEntryApp::create_reject_message(WireReject msg) {
  return fix_oe_core_->create_reject_message(msg);
}
void FixOrderEntryApp::post_new_order(const trading::NewSingleOrderData&) {}
void FixOrderEntryApp::post_cancel_order(const trading::OrderCancelRequest&) {}
void FixOrderEntryApp::post_cancel_and_reorder(
    const trading::OrderCancelAndNewOrderSingle&) {}
void FixOrderEntryApp::post_mass_cancel_order(
    const trading::OrderMassCancelRequest&) {}

FixOrderEntryApp::WireMessage FixOrderEntryApp::decode(
    const std::string& message) {
  return fix_oe_core_->decode(message);
}
}  // namespace core
