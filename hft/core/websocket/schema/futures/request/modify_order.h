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

#ifndef MODIFY_ORDER_H
#define MODIFY_ORDER_H
#include <glaze/glaze.hpp>

namespace schema::futures {
struct ModifyOrderParams {
  std::string symbol;
  std::string side;

  std::uint64_t origin_client_order_id{};
  std::uint64_t timestamp{};

  std::optional<std::string> api_key;
  std::optional<std::string> orig_type;
  std::optional<std::string> position_side;

  double price;
  std::optional<std::string> price_match;
  double quantity;

  std::optional<std::string> signature;

  // clang-format off
  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = ModifyOrderParams;
    static constexpr auto value = glz::object(  // NOLINT(readability-identifier-naming)
      "symbol",                 &T::symbol,
      "side",                   &T::side,
      "origClientOrderId",      &T::origin_client_order_id,
      "timestamp",              &T::timestamp,

      "apiKey",       &T::api_key,
      "origType",     &T::orig_type,
      "positionSide", &T::position_side,

      "price",        glz::quoted_num<&T::price>,
      "priceMatch",   &T::price_match,
      "quantity",     glz::quoted_num<&T::quantity>,

      "signature",    &T::signature
    );
  };
  // clang-format on
};

struct OrderModifyRequest {
  std::string id;
  std::string method = "order.modify";
  ModifyOrderParams params;

  // clang-format off
  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = OrderModifyRequest;
    static constexpr auto value = glz::object(  // NOLINT(readability-identifier-naming)
      "id",     &T::id,
      "method", &T::method,
      "params", &T::params
    );
  };
  // clang-format on
};
}  // namespace schema::futures
#endif  // MODIFY_ORDER_H