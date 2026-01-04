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

#ifndef BINANCE_FUTURES_OE_ENCODER_H
#define BINANCE_FUTURES_OE_ENCODER_H

#include "authorization.h"
#include "common.h"
#include "common/fixed_point.hpp"
#include "common/logger.h"
#include "order_entry.h"

#include <glaze/glaze.hpp>

#include "oe_id_constants.h"
#include "schema/futures/request/cancel_order.h"
#include "schema/futures/request/modify_order.h"
#include "schema/futures/request/new_order.h"
#include "schema/futures/request/userdata_stream_request.h"

namespace core {

class BinanceFuturesOeEncoder {
 public:
  explicit BinanceFuturesOeEncoder(const common::Logger::Producer& logger)
      : logger_(logger) {}

  [[nodiscard]] std::string create_log_on_message(const std::string& signature,
      const std::string& timestamp) const {
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
        R"({{"id":"{}{}","method":"session.logon","params":{{"apiKey":"{}","signature":"{}","timestamp":{}}}}})",
        oe_id::kLogin,
        ts_value,
        AUTHORIZATION.get_api_key(),
        signature,
        ts_value);

    LOG_INFO(logger_, "[WsOeCore] session.logon 요청 생성");
    return request;
  }
  [[nodiscard]] std::string create_log_out_message() const {
    const auto timestamp = util::get_timestamp_epoch();

    const std::string request =
        std::format(R"({{"id":"o{}","method":"session.logout","params":{{}}}})",
            timestamp);

    LOG_INFO(logger_, "[WsOeCore] session.logout 요청 생성");
    return request;
  }
  [[nodiscard]] std::string create_heartbeat_message() const { return ""; }
  [[nodiscard]] std::string create_order_all_cancel(
      const trading::OrderMassCancelRequest& /*request*/) const {
    return "";
  }

  [[nodiscard]] std::string create_user_data_stream_subscribe() const {
    const auto timestamp = std::to_string(util::get_timestamp_epoch());
    schema::futures::UserDataStreamStartRequest request;
    request.id = std::string(1, oe_id::kSubscribe) + timestamp;
    request.params.apiKey = AUTHORIZATION.get_api_key();

    const auto request_str = glz::write_json(request).value_or("error");
    if (UNLIKELY(request_str == "error")) {
      LOG_ERROR(logger_, "[WsOeCore] userDataStream.start 요청 실패");
    } else {
      LOG_INFO(logger_, "[WsOeCore] userDataStream.start 요청 생성");
    }

    return request_str;
  }
  [[nodiscard]] std::string create_user_data_stream_unsubscribe() const {
    const auto timestamp = std::to_string(util::get_timestamp_epoch());
    schema::futures::UserDataStreamStopRequest request;
    request.id = std::string(1, oe_id::kUnsubscribe) + timestamp;
    request.params.apiKey = AUTHORIZATION.get_api_key();

    const auto request_str = glz::write_json(request).value_or("error");
    if (UNLIKELY(request_str == "error")) {
      LOG_ERROR(logger_, "[WsOeCore] userDataStream.stop 요청 실패");
    } else {
      LOG_INFO(logger_, "[WsOeCore] userDataStream.stop 요청 생성");
    }

    return request_str;
  }
  [[nodiscard]] std::string create_user_data_stream_ping() const {
    const auto timestamp = std::to_string(util::get_timestamp_epoch());
    schema::futures::UserDataStreamPingRequest request;
    request.id = std::string(1, oe_id::kPing) + timestamp;
    request.params.apiKey = AUTHORIZATION.get_api_key();

    const auto request_str = glz::write_json(request).value_or("error");
    if (UNLIKELY(request_str == "error")) {
      LOG_ERROR(logger_, "[WsOeCore] userDataStream.ping 요청 실패");
    } else {
      LOG_TRACE(logger_, "[WsOeCore] userDataStream.ping 요청 생성");
    }

    return request_str;
  }

  [[nodiscard]] std::string create_order_message(
      const trading::NewSingleOrderData& order) const {
    schema::futures::OrderPlaceRequest payload;
    payload.id = std::string(1, oe_id::kOrderPlace) +
                 std::to_string(order.cl_order_id.value);

    payload.params.symbol = order.symbol;
    payload.params.new_client_order_id =
        std::to_string(order.cl_order_id.value);
    payload.params.side = std::string(toString(order.side));
    payload.params.type = std::string(toString(order.ord_type));
    payload.params.quantity = common::qty_to_actual_double(order.order_qty);

    if (order.ord_type == trading::OrderType::kLimit) {
      payload.params.time_in_force = std::string(toString(order.time_in_force));
      payload.params.price = common::price_to_actual_double(order.price);
    }
    payload.params.self_trade_prevention_mode =
        toString(order.self_trade_prevention_mode);

    if (order.position_side) {
      payload.params.position_side = common::toString(*order.position_side);
    }

    payload.params.timestamp = util::get_timestamp_epoch();

    const auto json_str = glz::write_json(payload).value_or(std::string{});

    return json_str;
  }

  [[nodiscard]] std::string create_cancel_order_message(
      const trading::OrderCancelRequest& cancel) const {
    schema::futures::OrderCancelRequest payload;
    payload.id = std::string(1, oe_id::kOrderCancel) +
                 std::to_string(cancel.cl_order_id.value);

    payload.params.symbol = cancel.symbol;
    payload.params.client_order_id =
        std::to_string(cancel.orig_cl_order_id.value);

    if (cancel.position_side) {
      payload.params.position_side = common::toString(*cancel.position_side);
    }

    payload.params.timestamp = util::get_timestamp_epoch();
    return glz::write_json(payload).value_or(std::string{});
  }
  [[nodiscard]] std::string create_cancel_and_reorder_message(
      const trading::OrderCancelAndNewOrderSingle& replace) const {
    schema::futures::OrderModifyRequest payload;
    payload.id = std::string(1, oe_id::kOrderReplace) +
                 std::to_string(replace.cl_new_order_id.value);

    payload.params.symbol = replace.symbol;
    payload.params.side = toString(replace.side);
    payload.params.origin_client_order_id = replace.cl_origin_order_id.value;
    payload.params.timestamp = util::get_timestamp_epoch();

    payload.params.quantity = common::qty_to_actual_double(replace.order_qty);
    if (replace.ord_type == trading::OrderType::kLimit) {
      payload.params.price = common::price_to_actual_double(replace.price);
    }

    if (replace.position_side) {
      payload.params.position_side = common::toString(*replace.position_side);
    }

    return glz::write_json(payload).value_or(std::string{});
  }

  [[nodiscard]] std::string create_modify_order_message(
      const trading::OrderModifyRequest& modify) const {
    schema::futures::OrderModifyRequest payload;
    payload.id = std::string(1, oe_id::kOrderModify) +
                 std::to_string(modify.orig_client_order_id.value);

    payload.params.symbol = modify.symbol;
    payload.params.side = toString(modify.side);
    payload.params.origin_client_order_id = modify.orig_client_order_id.value;
    payload.params.price = common::price_to_actual_double(modify.price);
    payload.params.quantity = common::qty_to_actual_double(modify.order_qty);

    if (modify.position_side) {
      payload.params.position_side = common::toString(*modify.position_side);
    }

    payload.params.timestamp = util::get_timestamp_epoch();
    return glz::write_json(payload).value_or(std::string{});
  }

 private:
  const common::Logger::Producer& logger_;
};

}  // namespace core

#endif  //BINANCE_FUTURES_OE_ENCODER_H
