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

#include "ws_oe_core.h"

#include "common/authorization.h"
#include "core/common.h"
#include "core/response_manager.h"
#include "core/signature.h"

#include "schema/request/cancel_all_orders.h"
#include "schema/request/cancel_and_reorder.h"
#include "schema/request/order_cancel.h"
#include "schema/request/order_request.h"
#include "schema/request/session.h"

namespace core {
WsOeCore::WsOeCore(common::Logger* logger,
    trading::ResponseManager* response_manager)
    : logger_(logger->make_producer()),
      mapper_(logger_, response_manager),
      decoder_(logger_),
      encoder_(logger_),
      response_manager_(response_manager) {}

WsOeCore::~WsOeCore() = default;

std::string WsOeCore::create_log_on_message(const std::string& signature,
    const std::string& timestamp) const {
  return encoder_.create_log_on_message(signature, timestamp);
}

std::string WsOeCore::create_log_out_message() const {
  return encoder_.create_log_out_message();
}

std::string WsOeCore::create_heartbeat_message() const {
  return encoder_.create_heartbeat_message();
}

std::string WsOeCore::create_user_data_stream_subscribe() const {
  return encoder_.create_user_data_stream_subscribe();
}

std::string WsOeCore::create_user_data_stream_unsubscribe() const {
  return encoder_.create_user_data_stream_unsubscribe();
}

std::string WsOeCore::create_order_message(
    const trading::NewSingleOrderData& order) const {
  return encoder_.create_order_message(order);
}

std::string WsOeCore::create_cancel_order_message(
    const trading::OrderCancelRequest& cancel) const {
  return encoder_.create_cancel_order_message(cancel);
}

std::string WsOeCore::create_cancel_and_reorder_message(
    const trading::OrderCancelRequestAndNewOrderSingle& replace) const {
  return encoder_.create_cancel_and_reorder_message(replace);
}

std::string WsOeCore::create_order_all_cancel(
    const trading::OrderMassCancelRequest& request) const {
  return encoder_.create_order_all_cancel(request);
}

trading::ExecutionReport* WsOeCore::create_execution_report_message(
    const WireExecutionReport& msg) const {
  return mapper_.to_execution_report(msg);
}

trading::OrderCancelReject* WsOeCore::create_order_cancel_reject_message(
    const WireCancelReject& msg) const {
  return mapper_.to_cancel_reject(msg);
}

trading::OrderMassCancelReport*
WsOeCore::create_order_mass_cancel_report_message(
    const WireMassCancelReport& msg) const {
  return mapper_.to_mass_cancel_report(msg);
}

trading::OrderReject WsOeCore::create_reject_message(
    const WireReject& msg) const {
  return mapper_.to_reject(msg);
}

WsOeCore::WireMessage WsOeCore::decode(std::string_view payload) const {
  return decoder_.decode(payload);
}

}  // namespace core
