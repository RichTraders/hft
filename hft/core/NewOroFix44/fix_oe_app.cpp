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

std::string FixOrderEntryApp::create_order_message(const trading::NewSingleOrderData& order_data) {
  return fix_oe_core_->create_order_message(order_data);
}

trading::ExecutionReport FixOrderEntryApp::create_execution_report_message(FIX8::NewOroFix44OE::ExecutionReport* msg) {
  return fix_oe_core_->create_excution_report_message(msg);
}

FIX8::Message* FixOrderEntryApp::decode(const std::string& message) {
  return fix_oe_core_->decode(message);
}
}