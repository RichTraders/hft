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

#ifndef CANCEL_AND_REORDER_H
#define CANCEL_AND_REORDER_H
#include <glaze/glaze.hpp>

namespace schema {

struct CancelReplaceOrderParams {
  std::string symbol;
  std::string cancel_replace_mode = "STOP_ON_FAILURE";
  std::string side;
  std::string type;

  std::uint64_t timestamp{};

  std::optional<std::string> api_key;
  std::optional<std::string> signature;

  std::optional<std::int64_t> cancel_order_id;
  std::optional<std::string> cancel_orig_client_order_id;
  std::optional<std::string> cancel_new_client_order_id;

  std::optional<std::string> time_in_force;
  std::optional<std::string> price;
  std::optional<std::string> quantity;
  std::optional<double> quote_order_qty;
  std::optional<std::string> new_client_order_id;
  std::optional<std::string> new_order_resp_type;
  std::optional<double> stop_price;
  std::optional<double> trailing_delta;
  std::optional<double> iceberg_qty;
  std::optional<std::int64_t> strategy_id;
  std::optional<int> strategy_type;
  std::optional<std::string> self_trade_prevention_mode;

  std::optional<std::string> cancel_restrictions;
  std::optional<std::string> order_rate_limit_exceeded_mode;
  std::optional<std::string> peg_price_type;
  std::optional<int> peg_offset_value;
  std::optional<std::string> peg_offset_type;
  std::optional<double> recv_window;

  // clang-format off
  struct glaze {
    using T = CancelReplaceOrderParams;
    static constexpr auto value = glz::object(
      "symbol",           &T::symbol,
      "cancelReplaceMode",&T::cancel_replace_mode,
      "side",             &T::side,
      "type",             &T::type,

      "cancelOrderId",        &T::cancel_order_id,
      "cancelOrigClientOrderId", &T::cancel_orig_client_order_id,
      "cancelNewClientOrderId",  &T::cancel_new_client_order_id,

      "timeInForce",   &T::time_in_force,
      "price",         glz::quoted_num<&T::price>,
      "quantity",      glz::quoted_num<&T::quantity>,
      "quoteOrderQty", glz::quoted_num<&T::quote_order_qty>,
      "newClientOrderId", &T::new_client_order_id,
      "newOrderRespType", &T::new_order_resp_type,
      "stopPrice",     glz::quoted_num<&T::stop_price>,
      "trailingDelta", glz::quoted_num<&T::trailing_delta>,
      "icebergQty",    glz::quoted_num<&T::iceberg_qty>,
      "strategyId",    &T::strategy_id,
      "strategyType",  &T::strategy_type,
      "selfTradePreventionMode", &T::self_trade_prevention_mode,

      "cancelRestrictions",        &T::cancel_restrictions,
      "orderRateLimitExceededMode",&T::order_rate_limit_exceeded_mode,
      "pegPriceType",              &T::peg_price_type,
      "pegOffsetValue",            &T::peg_offset_value,
      "pegOffsetType",             &T::peg_offset_type,

      "apiKey",     &T::api_key,
      "recvWindow", glz::quoted_num<&T::recv_window>,
      "signature",  &T::signature,
      "timestamp",  &T::timestamp
    );
  };
  // clang-format on
};

struct OrderCancelReplaceRequest {
  std::string id;
  std::string method = "order.cancelReplace";
  CancelReplaceOrderParams params;

  // clang-format off
  struct glaze {
    using T = OrderCancelReplaceRequest;
    static constexpr auto value = glz::object(
      "id",     &T::id,
      "method", &T::method,
      "params", &T::params
    );
  };
  // clang-format on
};

}  // namespace schema
#endif
