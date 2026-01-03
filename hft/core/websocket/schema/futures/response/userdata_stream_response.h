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

#ifndef FUTURES_USERDATA_STREAM_RESPONSE_H
#define FUTURES_USERDATA_STREAM_RESPONSE_H

#include <glaze/glaze.hpp>
#include "api_response.h"
namespace schema::futures {
struct UserDataStreamStartResult {
  std::string listen_key;

  // clang-format off
  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = UserDataStreamStartResult;
    static constexpr auto value = glz::object(  // NOLINT(readability-identifier-naming)
        "listenKey", &T::listen_key
    );
  };
  // clang-format on
};

struct UserDataStreamStartResponse {
  std::string id;
  int status;
  std::optional<UserDataStreamStartResult> result;
  std::optional<std::vector<RateLimit>> rate_limits;

  // clang-format off
  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = UserDataStreamStartResponse;
    static constexpr auto value = glz::object(  // NOLINT(readability-identifier-naming)
        "id", &T::id,
        "status", &T::status,
        "result", &T::result,
        "rateLimits", &T::rate_limits
    );
  };
  // clang-format on
};

struct UserDataStreamStopResult {
  // clang-format off
  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = UserDataStreamStopResult;
    static constexpr auto value = glz::object();  // NOLINT(readability-identifier-naming)  // NOLINT(readability-identifier-naming)
  };
  // clang-format on
};

struct UserDataStreamStopResponse {
  std::string id;
  int status;
  UserDataStreamStopResult result;
  std::optional<std::vector<RateLimit>> rate_limits;

  // clang-format off
  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = UserDataStreamStopResponse;
    static constexpr auto value = glz::object(  // NOLINT(readability-identifier-naming)
        "id", &T::id,
        "status", &T::status,
        "result", &T::result,
        "rateLimits", &T::rate_limits
    );
  };
  // clang-format on
};

struct UserDataStreamPingResult {
  std::string listen_key;

  // clang-format off
  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = UserDataStreamPingResult;
    static constexpr auto value = glz::object(  // NOLINT(readability-identifier-naming)
        "listenKey", &T::listen_key
    );
  };
  // clang-format on
};

struct UserDataStreamPingResponse {
  std::string id;
  int status;
  UserDataStreamPingResult result;
  std::optional<std::vector<RateLimit>> rate_limits;

  // clang-format off
  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = UserDataStreamPingResponse;
    static constexpr auto value = glz::object(  // NOLINT(readability-identifier-naming)
        "id", &T::id,
        "status", &T::status,
        "result", &T::result,
        "rateLimits", &T::rate_limits
    );
  };
  // clang-format on
};
}  // namespace schema::futures
#endif  // FUTURES_USERDATA_STREAM_RESPONSE_H
