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

#include "schema/futures/request/cancel_order.h"
#include "schema/futures/request/modify_order.h"
#include "schema/futures/request/new_order.h"
#include "schema/futures/request/userdata_stream_request.h"

namespace core {


inline std::string BinanceFuturesOeEncoder::create_log_on_message(
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


inline std::string BinanceFuturesOeEncoder::create_log_out_message() const {
  const auto timestamp = util::get_timestamp_epoch();

  const std::string request = std::format(
      R"({{"id":"logout_{}","method":"session.logout","params":{{}}}})",
      timestamp);

  logger_.info("[WsOeCore] session.logout 요청 생성");
  return request;
}


inline std::string BinanceFuturesOeEncoder::create_heartbeat_message() const {
  return "";
}


inline std::string BinanceFuturesOeEncoder::create_user_data_stream_subscribe() const {
  const auto timestamp = std::to_string(util::get_timestamp_epoch());
  schema::futures::UserDataStreamStartRequest request;
  request.id = "userDataStream_" + timestamp;
  request.params.apiKey = AUTHORIZATION.get_api_key();

  const auto request_str = glz::write_json(request).value_or("error");
  if (UNLIKELY(request_str == "error")) {
    logger_.error("[WsOeCore] userDataStream.start 요청 실패");
  } else {
    logger_.info("[WsOeCore] userDataStream.start 요청 생성");
  }

  return request_str;
}


inline std::string BinanceFuturesOeEncoder::create_user_data_stream_unsubscribe() const {
  const auto timestamp = std::to_string(util::get_timestamp_epoch());
  schema::futures::UserDataStreamStopRequest request;
  request.id = "unsubscribe_" + timestamp;
  request.params.apiKey = AUTHORIZATION.get_api_key();

  const auto request_str = glz::write_json(request).value_or("error");
  if (UNLIKELY(request_str == "error")) {
    logger_.error("[WsOeCore] userDataStream.stop 요청 실패");
  } else {
    logger_.info("[WsOeCore] userDataStream.stop 요청 생성");
  }

  return request_str;
}


inline std::string BinanceFuturesOeEncoder::create_order_message(
    const trading::NewSingleOrderData& order) const {
  schema::futures::OrderPlaceRequest payload;
  payload.id = "orderplace_" + std::to_string(order.cl_order_id.value);

  payload.params.symbol = order.symbol;
  payload.params.new_client_order_id = std::to_string(order.cl_order_id.value);
  payload.params.side = std::string(toString(order.side));
  payload.params.type = std::string(toString(order.ord_type));
  payload.params.quantity = order.order_qty.value;

  if (order.ord_type == trading::OrderType::kLimit) {
    payload.params.time_in_force = std::string(toString(order.time_in_force));
    payload.params.price = order.price.value;
  }
  payload.params.self_trade_prevention_mode =
      toString(order.self_trade_prevention_mode);

  payload.params.timestamp = util::get_timestamp_epoch();
  return glz::write_json(payload).value_or(std::string{});
}


inline std::string BinanceFuturesOeEncoder::create_cancel_order_message(
    const trading::OrderCancelRequest& cancel) const {
  schema::futures::OrderCancelRequest payload;
  payload.id = "ordercancel_" + std::to_string(cancel.cl_order_id.value);

  payload.params.symbol = cancel.symbol;
  payload.params.client_order_id =
      std::to_string(cancel.orig_cl_order_id.value);

  payload.params.timestamp = util::get_timestamp_epoch();
  return glz::write_json(payload).value_or(std::string{});
}


inline std::string BinanceFuturesOeEncoder::create_cancel_and_reorder_message(
    const trading::OrderCancelAndNewOrderSingle& replace) const {
  schema::futures::OrderModifyRequest payload;
  payload.id = "orderreplace_" + std::to_string(replace.cl_new_order_id.value);

  payload.params.symbol = replace.symbol;
  payload.params.side = toString(replace.side);
  payload.params.order_id = replace.cl_origin_order_id.value;
  payload.params.timestamp = util::get_timestamp_epoch();

  payload.params.quantity = replace.order_qty.value;
  if (replace.ord_type == trading::OrderType::kLimit) {
    payload.params.price = replace.price.value;
  }

  return glz::write_json(payload).value_or(std::string{});
}

}  // namespace core
