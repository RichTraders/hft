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

#ifndef CANCEL_ORDER_H
#define CANCEL_ORDER_H

#include <glaze/glaze.hpp>

namespace schema::futures {
struct CancelOrderParams {
  std::string symbol;
  std::optional<std::string> client_order_id;
  std::optional<std::string> position_side;  // For futures trading

  std::uint64_t timestamp;
  std::optional<std::string> api_key;
  std::optional<std::string> signature;

  // clang-format off
  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = CancelOrderParams;
    static constexpr auto value = glz::object(  // NOLINT(readability-identifier-naming)
      "symbol",         &T::symbol,
      "timestamp",      &T::timestamp,

      "apiKey",             &T::api_key,
      "signature",          &T::signature,
      "origClientOrderId",  &T::client_order_id,
      "positionSide",       &T::position_side
    );
  };
  // clang-format on
};

struct OrderCancelRequest {
  std::string id;
  std::string method = "order.cancel";
  CancelOrderParams params;

  // clang-format off
  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = OrderCancelRequest;
    static constexpr auto value = glz::object(  // NOLINT(readability-identifier-naming)
      "id",     &T::id,
      "method", &T::method,
      "params", &T::params
    );
  };
  // clang-format on
};
}  // namespace schema::futures
#endif  //CANCEL_ORDER_H
