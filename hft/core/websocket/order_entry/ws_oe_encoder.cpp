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

#include "ws_oe_encoder.h"

#include "authorization.h"
#include "common.h"

#include <glaze/glaze.hpp>

#include "schema/request/cancel_all_orders.h"
#include "schema/request/cancel_and_reorder.h"
#include "schema/request/order_cancel.h"
#include "schema/request/order_request.h"
#include "schema/request/session.h"
std::string core::WsOeEncoder::create_log_on_message(
    const std::string& signature, const std::string& timestamp) const {
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
std::string core::WsOeEncoder::create_log_out_message() const {
  const auto timestamp = util::get_timestamp_epoch();

  const std::string request = std::format(
      R"({{"id":"logout_{}","method":"session.logout","params":{{}}}})",
      timestamp);

  logger_.info("[WsOeCore] session.logout 요청 생성");
  return request;
}
std::string core::WsOeEncoder::create_heartbeat_message() const {
  return "";
}
std::string core::WsOeEncoder::create_user_data_stream_subscribe() const {
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
std::string core::WsOeEncoder::create_user_data_stream_unsubscribe() const {
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
std::string core::WsOeEncoder::create_order_message(
    const trading::NewSingleOrderData& order) const {
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
std::string core::WsOeEncoder::create_cancel_order_message(
    const trading::OrderCancelRequest& cancel) const {
  schema::OrderCancelRequest payload;
  payload.id = "order_cancel" + std::to_string(cancel.cl_order_id.value);

  payload.params.symbol = cancel.symbol;
  payload.params.new_client_order_id = std::to_string(cancel.cl_order_id.value);
  payload.params.orig_client_order_id =
      std::to_string(cancel.orig_cl_order_id.value);

  payload.params.timestamp = util::get_timestamp_epoch();
  return glz::write_json(payload).value_or(std::string{});
}
std::string core::WsOeEncoder::create_cancel_and_reorder_message(
    const trading::OrderCancelRequestAndNewOrderSingle& replace) const {
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
std::string core::WsOeEncoder::create_order_all_cancel(
    const trading::OrderMassCancelRequest& request) const {
  schema::OpenOrdersCancelAllRequest payload;

  payload.id = "order_cancelAll" + std::to_string(request.cl_order_id.value);
  payload.params.symbol = request.symbol;
  payload.params.timestamp = util::get_timestamp_epoch();

  return glz::write_json(payload).value_or(std::string{});
}