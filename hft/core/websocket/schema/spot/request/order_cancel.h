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

#ifndef ORDER_CANCEL_H
#define ORDER_CANCEL_H
#include <glaze/glaze.hpp>

namespace schema {
struct CancelOrderParams {
  std::string symbol;
  std::uint64_t timestamp{};

  std::optional<std::string> api_key;
  std::optional<std::string> signature;

  std::optional<std::int64_t> order_id;
  std::optional<std::string> orig_client_order_id;
  std::optional<std::string> new_client_order_id;
  std::optional<std::string> cancel_restrictions;
  std::optional<double> recv_window;

  // clang-format off
  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = CancelOrderParams;
    static constexpr auto value = glz::object(  // NOLINT(readability-identifier-naming)
        "symbol", &T::symbol,
        "orderId", &T::order_id,
        "origClientOrderId", &T::orig_client_order_id,
        "newClientOrderId", &T::new_client_order_id,
        "cancelRestrictions", &T::cancel_restrictions,
        "apiKey", &T::api_key,
        "recvWindow", glz::quoted_num<&T::recv_window>,
        "signature", &T::signature,
        "timestamp", &T::timestamp);
  };
  // clang-format on
};

struct OrderCancelRequest {
  std::string id;
  std::string method = "order.cancel";
  CancelOrderParams params;

  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = OrderCancelRequest;
    static constexpr auto value =  // NOLINT(readability-identifier-naming)
        glz::object("id", &T::id, "method", &T::method, "params", &T::params);
  };
};


}  // namespace schema
#endif
