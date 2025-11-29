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

#ifndef API_RESPONSE_H
#define API_RESPONSE_H

#include <glaze/glaze.hpp>

namespace schema {
struct RateLimit {
  std::string rate_limit_type;
  std::string interval;
  int intervalNum;
  int limit;
  std::optional<int> count;

  struct glaze {
    using T = RateLimit;
    static constexpr auto value = glz::object(
      "rateLimitType", &T::rate_limit_type,
      "interval", &T::interval,
      "intervalNum", &T::intervalNum,
      "limit", &T::limit,
      "count", &T::count);
  };
};

struct ErrorResponse {
  std::int32_t code;
  std::string message;
  struct glaze {
    using T = ErrorResponse;
    static constexpr auto value =
        glz::object("code", &T::code, "msg", &T::message);
  };
};

struct ApiResponse {
  std::string id;
  int status{0};
  std::optional<ErrorResponse> error;
  std::optional<std::vector<RateLimit>> rate_limits;

  // clang-format off
  struct glaze {
    using T = ApiResponse;
    static constexpr auto value = glz::object(
      "id", &T::id,
      "status", &T::status,
      "error", &T::error,
      "rateLimits", &T::rate_limits);
  };
  // clang-format on
};

struct WsHeader {
  std::string id;
  std::uint32_t status;

  struct glaze {
    using T = WsHeader;
    static constexpr auto value =
        glz::object("id", &T::id, "status", &T::status);
  };
};
}  // namespace schema
#endif  //API_RESPONSE_H
