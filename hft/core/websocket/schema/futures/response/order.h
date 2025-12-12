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

#ifndef FUTURES_ORDER_H
#define FUTURES_ORDER_H
#include <glaze/glaze.hpp>
#include "api_response.h"

namespace schema {
namespace futures {
struct OrderResult {
  std::uint64_t order_id{};
  std::string symbol;
  std::string status;
  std::string client_order_id;

  double price;
  double avg_price;
  double orig_qty;
  double executed_qty;
  double cum_qty;
  double cum_quote;

  std::string time_in_force;
  std::string type;

  bool reduce_only{};
  bool close_position{};

  std::string side;
  std::string position_side;
  std::string stop_price;
  std::string working_type;

  bool price_protect{};

  std::string orig_type;
  std::string price_match;
  std::string self_trade_prevention_mode;

  std::int64_t good_till_date{};
  std::int64_t update_time{};

  // clang-format off
  struct glaze {
    using T = OrderResult;
    static constexpr auto value = glz::object(
      "orderId",        &T::order_id,
      "symbol",         &T::symbol,
      "status",         &T::status,
      "clientOrderId",  &T::client_order_id,
      "price",          glz::quoted_num<&T::price>,
      "avgPrice",       glz::quoted_num<&T::avg_price>,
      "origQty",        glz::quoted_num<&T::orig_qty>,
      "executedQty",    glz::quoted_num<&T::executed_qty>,
      "cumQty",         glz::quoted_num<&T::cum_qty>,
      "cumQuote",       glz::quoted_num<&T::cum_quote>,
      "timeInForce",    &T::time_in_force,
      "type",           &T::type,
      "reduceOnly",     &T::reduce_only,
      "closePosition",  &T::close_position,
      "side",           &T::side,
      "positionSide",   &T::position_side,
      "stopPrice",      &T::stop_price,
      "workingType",    &T::working_type,
      "priceProtect",   &T::price_protect,
      "origType",       &T::orig_type,
      "priceMatch",     &T::price_match,
      "selfTradePreventionMode", &T::self_trade_prevention_mode,
      "goodTillDate",   &T::good_till_date,
      "updateTime",     &T::update_time
    );
  };
  // clang-format on
};

struct PlaceOrderResponse {
  std::string id;
  int status{};
  OrderResult result;
  std::vector<RateLimit> rate_limits;

  struct glaze {
    using T = PlaceOrderResponse;
    static constexpr auto value = glz::object("id", &T::id, "status",
        &T::status, "result", &T::result, "rateLimits", &T::rate_limits);
  };
};
}  // namespace futures
}  // namespace schema
#endif  //FUTURES_ORDER_H