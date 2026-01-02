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

#include "authorization.h"
#include "common.h"

#include <glaze/glaze.hpp>

#include "schema/spot/request/cancel_all_orders.h"
#include "schema/spot/request/cancel_and_reorder.h"
#include "schema/spot/request/order_cancel.h"
#include "schema/spot/request/order_request.h"
#include "schema/spot/request/session.h"

namespace core {

inline std::string BinanceSpotOeEncoder::create_log_on_message(
    const std::string& signature, const std::string& timestamp) const {
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
      R"({{"id":"login_{}","method":"session.logon","params":{{"apiKey":"{}","signature":"{}","timestamp":{}}}}})",
      ts_value,
      AUTHORIZATION.get_api_key(),
      signature,
      ts_value);

  logger_.info("[WsOeCore] session.logon 요청 생성");
  return request;
}

inline std::string BinanceSpotOeEncoder::create_log_out_message() const {
  const auto timestamp = util::get_timestamp_epoch();

  const std::string request = std::format(
      R"({{"id":"logout_{}","method":"session.logout","params":{{}}}})",
      timestamp);

  logger_.info("[WsOeCore] session.logout 요청 생성");
  return request;
}

inline std::string BinanceSpotOeEncoder::create_heartbeat_message() const {
  return "";
}

inline std::string BinanceSpotOeEncoder::create_user_data_stream_subscribe()
    const {
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

inline std::string BinanceSpotOeEncoder::create_user_data_stream_unsubscribe()
    const {
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

inline std::string BinanceSpotOeEncoder::create_user_data_stream_ping() const {
  return "";
}

inline std::string BinanceSpotOeEncoder::create_order_message(
    const trading::NewSingleOrderData& order) const {
  schema::OrderPlaceRequest payload;
  payload.id = "orderplace_" + std::to_string(order.cl_order_id.value);

  payload.params.symbol = order.symbol;
  payload.params.new_client_order_id = std::to_string(order.cl_order_id.value);
  payload.params.side = std::string(toString(order.side));
  payload.params.type = std::string(toString(order.ord_type));
  payload.params.quantity = common::to_fixed(common::qty_to_actual_double(order.order_qty), qty_precision_);

  if (order.ord_type == trading::OrderType::kLimit) {
    payload.params.time_in_force = std::string(toString(order.time_in_force));
    payload.params.price = common::to_fixed(common::price_to_actual_double(order.price), price_precision_);
  }
  payload.params.self_trade_prevention_mode =
      toString(order.self_trade_prevention_mode);

  payload.params.timestamp = util::get_timestamp_epoch();
  return glz::write_json(payload).value_or(std::string{});
}

inline std::string BinanceSpotOeEncoder::create_cancel_order_message(
    const trading::OrderCancelRequest& cancel) const {
  schema::OrderCancelRequest payload;
  payload.id = "ordercancel_" + std::to_string(cancel.cl_order_id.value);

  payload.params.symbol = cancel.symbol;
  payload.params.new_client_order_id = std::to_string(cancel.cl_order_id.value);
  payload.params.orig_client_order_id =
      std::to_string(cancel.orig_cl_order_id.value);

  payload.params.timestamp = util::get_timestamp_epoch();
  return glz::write_json(payload).value_or(std::string{});
}

inline std::string BinanceSpotOeEncoder::create_cancel_and_reorder_message(
    const trading::OrderCancelAndNewOrderSingle& replace) const {
  schema::OrderCancelReplaceRequest payload;
  payload.id = "orderreplace_" + std::to_string(replace.cl_new_order_id.value);

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
  payload.params.quantity = common::to_fixed(common::qty_to_actual_double(replace.order_qty), qty_precision_);
  if (replace.ord_type == trading::OrderType::kLimit) {
    payload.params.time_in_force = std::string(toString(replace.time_in_force));
    payload.params.price = common::to_fixed(common::price_to_actual_double(replace.price), price_precision_);
  }
  payload.params.self_trade_prevention_mode =
      toString(replace.self_trade_prevention_mode);

  return glz::write_json(payload).value_or(std::string{});
}

inline std::string BinanceSpotOeEncoder::create_order_all_cancel(
    const trading::OrderMassCancelRequest& request) const {
  schema::OpenOrdersCancelAllRequest payload;

  payload.id = "ordercancelAll_" + std::to_string(request.cl_order_id.value);
  payload.params.symbol = request.symbol;
  payload.params.timestamp = util::get_timestamp_epoch();

  return glz::write_json(payload).value_or(std::string{});
}

}  // namespace core