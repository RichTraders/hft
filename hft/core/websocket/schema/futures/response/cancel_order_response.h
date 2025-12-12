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

#ifndef CANCEL_ORDER_RESPONSE_H
#define CANCEL_ORDER_RESPONSE_H

#include <glaze/glaze.hpp>
#include "api_response.h"
namespace schema {
namespace futures {
struct CancelOrderResult {
  std::uint64_t order_id{};
  std::string client_order_id;
  std::string symbol;
  std::string status;
  std::string side;
  std::string position_side;
  std::string type;
  std::string time_in_force;
  std::string orig_type;
  std::string working_type;
  std::string price_match;
  std::string self_trade_prevention_mode;

  double price{};
  double stop_price{};
  double activate_price{};
  double price_rate{};

  double orig_qty{};
  double executed_qty{};
  double cum_qty{};
  double cum_quote{};

  bool reduce_only{};
  bool close_position{};
  bool price_protect{};

  std::int64_t update_time{};
  std::int64_t good_till_date{};

  // clang-format off
    struct glaze {
        using T = CancelOrderResult;
        static constexpr auto value = glz::object(
            "orderId",                  &T::order_id,
            "clientOrderId",            &T::client_order_id,
            "symbol",                   &T::symbol,
            "status",                   &T::status,
            "side",                     &T::side,
            "positionSide",             &T::position_side,
            "type",                     &T::type,
            "timeInForce",              &T::time_in_force,
            "origType",                 &T::orig_type,
            "workingType",              &T::working_type,
            "priceMatch",               &T::price_match,
            "selfTradePreventionMode",  &T::self_trade_prevention_mode,

            "price",                    glz::quoted_num<&T::price>,
            "stopPrice",                glz::quoted_num<&T::stop_price>,
            "activatePrice",            glz::quoted_num<&T::activate_price>,
            "priceRate",                glz::quoted_num<&T::price_rate>,
            "origQty",                  glz::quoted_num<&T::orig_qty>,
            "executedQty",              glz::quoted_num<&T::executed_qty>,
            "cumQty",                   glz::quoted_num<&T::cum_qty>,
            "cumQuote",                 glz::quoted_num<&T::cum_quote>,

            "reduceOnly",               &T::reduce_only,
            "closePosition",            &T::close_position,
            "priceProtect",             &T::price_protect,

            "updateTime",               &T::update_time,
            "goodTillDate",             &T::good_till_date
        );
    };
  // clang-format on
};

struct CancelOrderResponse {
  std::string id;
  int status{};
  CancelOrderResult result;
  std::vector<RateLimit> rate_limits;

  // clang-format off
    struct glaze {
        using T = CancelOrderResponse;
        static constexpr auto value = glz::object(
            "id",           &T::id,
            "status",       &T::status,
            "result",       &T::result,
            "rateLimits",   &T::rate_limits
        );
    };
  // clang-format on
};
}  // namespace futures
}  // namespace schema
#endif  //CANCEL_ORDER_RESPONSE_H
