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

  // clang-format off
  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = RateLimit;
    static constexpr auto value = glz::object(  // NOLINT(readability-identifier-naming)  // NOLINT(readability-identifier-naming)
      "rateLimitType", &T::rate_limit_type,
      "interval", &T::interval,
      "intervalNum", &T::intervalNum,
      "limit", &T::limit,
      "count", &T::count);
  };
  // clang-format on
};

template <typename DataT = std::string>
struct ErrorResponse {
  std::int32_t code;
  std::string message;
  std::optional<DataT> data;
  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = ErrorResponse;
    // NOLINTNEXTLINE(readability-identifier-naming)
    static constexpr auto value =  // NOLINT(readability-identifier-naming)
        glz::object("code", &T::code, "msg", &T::message, "data", &T::data);
  };
};

struct ApiResponse {
  std::string id;
  int status{0};
  std::optional<ErrorResponse<>> error;
  std::optional<std::vector<RateLimit>> rate_limits;

  // clang-format off
  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = ApiResponse;
    static constexpr auto value = glz::object(  // NOLINT(readability-identifier-naming)  // NOLINT(readability-identifier-naming)
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

  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = WsHeader;
    // NOLINTNEXTLINE(readability-identifier-naming)
    static constexpr auto value =  // NOLINT(readability-identifier-naming)
        glz::object("id", &T::id, "status", &T::status);
  };
};
}  // namespace schema
#endif  //API_RESPONSE_H
