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

namespace schema {
namespace futures {
struct CancelOrderParams {
  std::optional<std::uint64_t> order_id{};
  std::string symbol;
  std::optional<std::string> client_order_id;

  std::uint64_t timestamp;
  std::optional<std::string> api_key;
  std::optional<std::string> signature;

  // clang-format off
  struct glaze {
    using T = CancelOrderParams;
    static constexpr auto value = glz::object(
      "symbol",         &T::symbol,
      "timestamp",      &T::timestamp,

      "apiKey",         &T::api_key,
      "signature",      &T::signature,
      "orderId",        &T::order_id,
      "clientOrderId",  &T::client_order_id
    );
  };
  // clang-format on
};

struct OrderCancelRequest {
  std::string id;
  std::string method = "order.place";
  CancelOrderParams params;

  // clang-format off
  struct glaze {
    using T = OrderCancelRequest;
    static constexpr auto value = glz::object(
      "id",     &T::id,
      "method", &T::method,
      "params", &T::params
    );
  };
  // clang-format on
};
}  // namespace futures
}  // namespace schema
#endif  //CANCEL_ORDER_H
