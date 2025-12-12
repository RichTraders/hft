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

#ifndef NEW_ORDER_H
#define NEW_ORDER_H
#include <glaze/glaze.hpp>

namespace schema {
namespace futures {
struct PlaceOrderParams {
  std::string symbol;
  std::string side;
  std::string type;
  std::int64_t timestamp{};

  std::optional<std::string> api_key;
  std::optional<std::string> signature;

  std::optional<std::string> position_side;

  double price;
  double quantity;

  std::optional<std::string> time_in_force;
  std::optional<std::string> reduce_only;
  std::optional<std::string> new_client_order_id;
  std::optional<double> stop_price;

  std::optional<std::string> close_position;

  std::optional<double> activation_price;
  std::optional<double> callback_rate;
  std::optional<std::string> working_type;

  std::optional<std::string> price_protect;

  std::optional<std::string> new_order_resp_type;
  std::optional<std::string> price_match;
  std::optional<std::string> self_trade_prevention_mode;

  std::optional<std::int64_t> good_till_date;
  std::optional<std::int64_t> recv_window;

  // clang-format off
  struct glaze {
    using T = PlaceOrderParams;
    static constexpr auto value = glz::object(
      // Mandatory
      "symbol",         &T::symbol,
      "side",           &T::side,
      "type",           &T::type,
      "timestamp",      &T::timestamp,

      // Authentication
      "apiKey",         &T::api_key,
      "signature",      &T::signature,

      // Optionals
      "positionSide",   &T::position_side,
      "price",          &T::price,
      "quantity",       &T::quantity,
      "timeInForce",    &T::time_in_force,
      "reduceOnly",     &T::reduce_only,
      "newClientOrderId", &T::new_client_order_id,
      "stopPrice",      &T::stop_price,
      "closePosition",  &T::close_position,
      "activationPrice",&T::activation_price,
      "callbackRate",   &T::callback_rate,
      "workingType",    &T::working_type,
      "priceProtect",   &T::price_protect,
      "newOrderRespType", &T::new_order_resp_type,
      "priceMatch",     &T::price_match,
      "selfTradePreventionMode", &T::self_trade_prevention_mode,
      "goodTillDate",   &T::good_till_date,
      "recvWindow",     &T::recv_window
    );
  };
  // clang-format on
};

struct OrderPlaceRequest {
  std::string id;
  std::string method = "order.place"; // 기본값
  PlaceOrderParams params;

  // clang-format off
  struct glaze {
    using T = OrderPlaceRequest;
    static constexpr auto value = glz::object(
      "id",     &T::id,
      "method", &T::method,
      "params", &T::params
    );
  };
  // clang-format on
};
}
}

#endif //NEW_ORDER_H