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

#ifndef CANCEL_ALL_ORDERS_H
#define CANCEL_ALL_ORDERS_H
#include <glaze/glaze.hpp>
namespace schema {

struct CancelAllOpenOrdersParams {
  std::string symbol;
  std::uint64_t timestamp{};

  std::optional<std::string> api_key;
  std::optional<std::string> signature;
  std::optional<double> recv_window;

  // clang-format off
  struct glaze {
    using T = CancelAllOpenOrdersParams;
    static constexpr auto value = glz::object(
      "symbol",    &T::symbol,
      "apiKey",    &T::api_key,
      "recvWindow", glz::quoted_num<&T::recv_window>,
      "signature", &T::signature,
      "timestamp", &T::timestamp
    );
  };
  // clang-format on
};

struct OpenOrdersCancelAllRequest {
  std::string id;
  const std::string method = "openOrders.cancelAll";
  CancelAllOpenOrdersParams params;

  // clang-format off
  struct glaze {
    using T = OpenOrdersCancelAllRequest;
    static constexpr auto value = glz::object(
      "id",     &T::id,
      "method", &T::method,
      "params", &T::params
    );
  };
  // clang-format on
};

}  // namespace schema
#endif  //CANCEL_ALL_ORDERS_H
