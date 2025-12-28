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

#ifndef ORDER_REQUEST_H
#define ORDER_REQUEST_H
#include <glaze/glaze.hpp>

namespace schema {
struct PlaceOrderParams {

  std::string symbol;
  std::string side;
  std::string type;

  std::uint64_t timestamp{};

  std::optional<std::string> api_key;
  std::optional<std::string> signature;

  std::optional<std::string> time_in_force;
  std::optional<std::string> price;
  std::optional<std::string> quantity;
  std::optional<double> quote_order_qty;
  std::optional<std::string> new_client_order_id;
  std::optional<std::string> new_order_resp_type;
  std::optional<double> stop_price;
  std::optional<int> trailing_delta;
  std::optional<double> iceberg_qty;
  std::optional<std::int64_t> strategy_id;
  std::optional<int> strategy_type;
  std::optional<std::string> self_trade_prevention_mode;
  std::optional<std::string> peg_price_type;
  std::optional<int> peg_offset_value;
  std::optional<std::string> peg_offset_type;

  // clang-format off
  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = PlaceOrderParams;
    static constexpr auto value = glz::object(  // NOLINT(readability-identifier-naming)
      
      "symbol",   &T::symbol,
      "side",     &T::side,
      "type",     &T::type,

      "apiKey",        &T::api_key,
      "timeInForce",   &T::time_in_force,
      "signature",     &T::signature,
      
      "price",         glz::quoted_num<&T::price>,
      "quantity",      glz::quoted_num<&T::quantity>,
      "quoteOrderQty", glz::quoted_num<&T::quote_order_qty>,
      "stopPrice",     glz::quoted_num<&T::stop_price>,
      "trailingDelta", &T::trailing_delta,
      "icebergQty",    glz::quoted_num<&T::iceberg_qty>,
      "strategyId",    &T::strategy_id,
      "strategyType",  &T::strategy_type,
      "selfTradePreventionMode", &T::self_trade_prevention_mode,
      "pegPriceType",  &T::peg_price_type,
      "pegOffsetValue",&T::peg_offset_value,
      "pegOffsetType", &T::peg_offset_type,

      "newClientOrderId", &T::new_client_order_id,
      "newOrderRespType", &T::new_order_resp_type,
      "timestamp", &T::timestamp
    );
  };
  // clang-format on
};

struct OrderPlaceRequest {
  std::string id;
  std::string method = "order.place";
  PlaceOrderParams params;

  // clang-format off
  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = OrderPlaceRequest;
    static constexpr auto value = glz::object(  // NOLINT(readability-identifier-naming)
      "id",     &T::id,
      "method", &T::method,
      "params", &T::params
    );
  };
  // clang-format on
};
}  // namespace schema
#endif
