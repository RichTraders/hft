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

#include "common.h"
#include "common/authorization.h"
#include "common/ini_config.hpp"
#include "core/response_manager.h"
#include "core/signature.h"
#include "schema/account_position.h"
#include "schema/execution_report.h"
#include "schema/request/cancel_and_reorder.h"
#include "schema/request/order_cancel.h"
#include "schema/request/order_request.h"
#include "schema/request/session.h"
#include "schema/response/order.h"

#include "schema/request/cancel_all_orders.h"
#include "schema/response/session_response.h"

using schema::ApiResponse;
using schema::CancelOrderResponse;
using schema::ExecutionReportResponse;
using schema::SessionLogonResponse;

namespace core {
WsOeCore::WsOeCore(common::Logger* logger,
    trading::ResponseManager* response_manager)
    : logger_(logger->make_producer()), response_manager_(response_manager) {}

WsOeCore::~WsOeCore() = default;

std::string WsOeCore::create_log_on_message(const std::string& signature,
    const std::string& timestamp) const {
  constexpr int64_t kRecvWindow = 5000;

  int64_t ts_value = 0;
  if (!timestamp.empty()) {
    try {
      ts_value = std::stoll(timestamp);
    } catch (const std::exception&) {
      ts_value = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
                     .count();
    }
  } else {
    ts_value = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch())
                   .count();
  }

  const std::string request = std::format(
      R"({{"id":"login_{}","method":"session.logon","params":{{"apiKey":"{}","signature":"{}","timestamp":{},"recvWindow":{}}}}})",
      ts_value,
      AUTHORIZATION.get_api_key(),
      signature,
      ts_value,
      kRecvWindow);

  logger_.info("[WsOeCore] session.logon 요청 생성");
  return request;
}

std::string WsOeCore::create_log_out_message() const {
  const auto timestamp = util::get_timestamp_epoch();

  const std::string request = std::format(
      R"({{"id":"logout_{}","method":"session.logout","params":{{}}}})",
      timestamp);

  logger_.info("[WsOeCore] session.logout 요청 생성");
  return request;
}

std::string WsOeCore::create_heartbeat_message() const {
  return {};
}

std::string WsOeCore::create_user_data_stream_subscribe() const {
  const auto timestamp = std::to_string(util::get_timestamp_epoch());
  const schema::SessionUserSubscriptionRequest request{
      "subscribe_" + timestamp};

  const auto request_str = glz::write_json(request).value_or("error");
  if (UNLIKELY(request_str == "error")) {
    logger_.error("[WsOeCore] userDataStream.subscribe 요청 실패");
  } else {
    logger_.info("[WsOeCore] userDataStream.subscribe 요청 생성");
  }

  return request_str;
}

std::string WsOeCore::create_user_data_stream_unsubscribe() const {
  const auto timestamp = std::to_string(util::get_timestamp_epoch());
  const schema::SessionUserUnsubscriptionRequest request{
      "unsubscribe_" + timestamp};

  const auto request_str = glz::write_json(request).value_or("error");
  if (UNLIKELY(request_str == "error")) {
    logger_.error("[WsOeCore] userDataStream.unsubscribe 요청 실패");
  } else {
    logger_.info("[WsOeCore] userDataStream.unsubscribe 요청 생성");
  }

  return request_str;
}

std::string WsOeCore::create_order_message(
    const trading::NewSingleOrderData& order) {
  schema::OrderPlaceRequest payload;
  payload.id = "order_place" + std::to_string(order.cl_order_id.value);

  payload.params.symbol = order.symbol;
  payload.params.new_client_order_id = std::to_string(order.cl_order_id.value);
  payload.params.side = std::string(toString(order.side));
  payload.params.type = std::string(toString(order.ord_type));
  payload.params.quantity = to_fixed(order.order_qty.value, kQtyPrecision);

  if (order.ord_type == trading::OrderType::kLimit) {
    payload.params.time_in_force = std::string(toString(order.time_in_force));
    payload.params.price = to_fixed(order.price.value, kPricePrecision);
  }
  payload.params.self_trade_prevention_mode =
      toString(order.self_trade_prevention_mode);

  payload.params.timestamp = util::get_timestamp_epoch();
  return glz::write_json(payload).value_or(std::string{});
}

std::string WsOeCore::create_cancel_order_message(
    const trading::OrderCancelRequest& cancel) {
  schema::OrderCancelRequest payload;
  payload.id = "order_cancel" + std::to_string(cancel.cl_order_id.value);

  payload.params.symbol = cancel.symbol;
  payload.params.new_client_order_id = std::to_string(cancel.cl_order_id.value);
  payload.params.orig_client_order_id =
      std::to_string(cancel.orig_cl_order_id.value);

  payload.params.timestamp = util::get_timestamp_epoch();
  return glz::write_json(payload).value_or(std::string{});
}

std::string WsOeCore::create_cancel_and_reorder_message(
    const trading::OrderCancelRequestAndNewOrderSingle& replace) {
  schema::OrderCancelReplaceRequest payload;
  payload.id = "order_replace" + std::to_string(replace.cl_new_order_id.value);

  payload.params.symbol = replace.symbol;
  payload.params.side = toString(replace.side);
  payload.params.type = toString(replace.ord_type);

  payload.params.timestamp = util::get_timestamp_epoch();

  payload.params.cancel_orig_client_order_id =
      std::to_string(replace.cl_origin_order_id.value);
  payload.params.cancel_new_client_order_id =
      std::to_string(replace.cancel_new_order_id.value);
  payload.params.new_client_order_id =
      std::to_string(replace.cl_new_order_id.value);
  payload.params.quantity = to_fixed(replace.order_qty.value, kQtyPrecision);
  if (replace.ord_type == trading::OrderType::kLimit) {
    payload.params.time_in_force = std::string(toString(replace.time_in_force));
    payload.params.price = to_fixed(replace.price.value, kPricePrecision);
  }
  payload.params.self_trade_prevention_mode =
      toString(replace.self_trade_prevention_mode);

  return glz::write_json(payload).value_or(std::string{});
}

std::string WsOeCore::create_order_all_cancel(
    const trading::OrderMassCancelRequest& request) {
  schema::OpenOrdersCancelAllRequest payload;

  payload.id = "order_cancelAll" + std::to_string(request.cl_order_id.value);
  payload.params.symbol = request.symbol;
  payload.params.timestamp = util::get_timestamp_epoch();

  return glz::write_json(payload).value_or(std::string{});
}

trading::ExecutionReport* WsOeCore::allocate_execution_report() const {
  return response_manager_->execution_report_allocate();
}

trading::OrderCancelReject* WsOeCore::allocate_cancel_reject() const {
  return response_manager_->order_cancel_reject_allocate();
}

trading::OrderMassCancelReport* WsOeCore::allocate_mass_cancel_report() const {
  return response_manager_->order_mass_cancel_report_allocate();
}

trading::ExecutionReport* WsOeCore::create_execution_report_message(
    const WireExecutionReport& msg) const {
  auto* report = allocate_execution_report();
  if (!report) {
    logger_.error("Failed to allocate execution report");
    return nullptr;
  }

  const auto& event = msg.event;
  report->cl_order_id = common::OrderId{event.client_order_id};
  report->symbol = event.symbol;
  report->exec_type = trading::toType(event.execution_type);
  report->ord_status = trading::toOrderStatus(event.order_status);
  report->cum_qty = common::Qty{event.cumulative_filled_quantity};
  report->leaves_qty = common::Qty{
      std::max(0.0, event.order_quantity - event.cumulative_filled_quantity)};
  report->last_qty = common::Qty{event.last_executed_quantity};
  report->price = common::Price{event.order_price};
  report->side = common::toSide(event.side);
  report->text = event.reject_reason;
  report->error_code = 0;

  return report;
}

trading::OrderCancelReject* WsOeCore::create_order_cancel_reject_message(
    const WireCancelReject& msg) const {
  auto* reject = allocate_cancel_reject();
  if (!reject) {
    logger_.error("Failed to allocate cancel reject");
    return nullptr;
  }
  reject->cl_order_id = common::OrderId{msg.event.client_order_id};
  reject->symbol = msg.event.symbol;
  reject->error_code = 0;
  reject->text = msg.event.reject_reason;
  return reject;
}

trading::OrderMassCancelReport*
WsOeCore::create_order_mass_cancel_report_message(
    const WireMassCancelReport& msg) const {
  auto* report = allocate_mass_cancel_report();
  if (!report) {
    logger_.error("Failed to allocate mass cancel report");
    return nullptr;
  }
  report->cl_order_id =
      common::OrderId{static_cast<uint64_t>(msg.event.client_order_id)};
  report->symbol = msg.event.symbol;
  report->mass_cancel_request_type = '7';
  report->mass_cancel_response =
      trading::MassCancelResponse::kCancelSymbolOrders;
  report->total_affected_orders = 0;
  report->error_code = 0;
  report->text = msg.event.reject_reason;
  return report;
}

trading::OrderReject WsOeCore::create_reject_message(
    const WireReject& msg) const {
  trading::OrderReject reject;
  reject.session_reject_reason = "WebSocket";
  reject.rejected_message_type = 0;
  if (msg.error.has_value()) {
    reject.error_code = msg.error.value().code;
    reject.error_message = msg.error.value().message;
  }
  return reject;
}

WsOeCore::WireMessage WsOeCore::decode(std::string_view payload) {
  if (payload.empty()) {
    return WireMessage{};
  }
  logger_.info(std::format("[WsOeCore]payload :{}", payload));

  if (payload.find("executionReport") != std::string_view::npos) {
    return decode_or_log<schema::ExecutionReportResponse>(payload,
        "[executionReport]");
  }
  if (payload.find("outboundAccountPosition") != std::string_view::npos) {
    return decode_or_log<schema::OutboundAccountPositionEnvelope>(payload,
        "[outboundAccountPosition]");
  }
  if (payload.find("balanceUpdate") != std::string_view::npos) {
    return decode_or_log<schema::BalanceUpdateEnvelope>(payload,
        "[balanceUpdate]");
  }

  schema::WsHeader header{};
  const auto error_code =
      glz::read<glz::opts{.error_on_unknown_keys = 0, .partial_read = 1}>(
          header,
          payload);
  if (error_code != glz::error_code::none) {
    logger_.error(std::format("Failed to decode payload"));
    return WireMessage{};
  }
  logger_.debug(std::format("[WsOeCore]header id :{}", header.id));

  if (header.id.starts_with("login_")) {
    return decode_or_log<schema::SessionLogonResponse>(payload,
        "[session.logon]");
  }

  if (header.id.starts_with("subscribe")) {
    return decode_or_log<schema::SessionUserSubscriptionResponse>(payload,
        "[userDataStream.subscribe]");
  }

  if (header.id.starts_with("unsubscribe")) {
    return decode_or_log<schema::SessionUserUnsubscriptionResponse>(payload,
        "[userDataStream.unsubscribe]");
  }

  if (header.id.starts_with("order")) {
    if (header.id.starts_with("order_replace")) {
      return decode_or_log<schema::CancelAndReorderResponse>(payload,
          "[cancelReplace]");
    }
    if (header.id.starts_with("order_cancelAll")) {
      return decode_or_log<schema::CancelAllOrdersResponse>(payload,
          "[cancelAll]");
    }
    if (header.id.starts_with("order_cancel")) {
      return decode_or_log<schema::CancelOrderResponse>(payload,
          "[orderCancel]");
    }
    return decode_or_log<schema::PlaceOrderResponse>(payload, "[orderPlace]");
  }

  return decode_or_log<schema::ApiResponse>(payload, "[API response]");
}

template <class T>
WsOeCore::WireMessage WsOeCore::decode_or_log(std::string_view payload,
    std::string_view label) {
  auto parsed = glz::read_json<T>(payload);
  if (!parsed) {
    auto error_msg = glz::format_error(parsed.error(), payload);
    logger_.error(
        std::format("\x1b[31m Failed to decode {} response: "
                    "{}. payload:{} \x1b[0m",
            label,
            error_msg,
            payload));
    return WireMessage{};
  }
  return WireMessage{std::in_place_type<T>, std::move(*parsed)};
}
}  // namespace core
