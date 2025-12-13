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

#ifndef FUTURES_SESSION_RESPONSE_H
#define FUTURES_SESSION_RESPONSE_H

#include <glaze/glaze.hpp>
#include "api_response.h"

namespace schema {
namespace futures {

struct SessionLogonResponse {
  std::string id;
  int status{0};
  std::optional<ErrorResponse<>> error;

  struct Result {
    std::string apiKey;
    std::uint64_t authorizedSince;
    std::uint64_t connectedSince;
    bool returnRateLimits;
    std::uint64_t serverTime;

    struct glaze {
      using T = Result;
      static constexpr auto value = glz::object(
        "apiKey", &T::apiKey,
        "authorizedSince", &T::authorizedSince,
        "connectedSince", &T::connectedSince,
        "returnRateLimits", &T::returnRateLimits,
        "serverTime", &T::serverTime
      );
    };
  };

  std::optional<Result> result;
  std::optional<std::vector<RateLimit>> rate_limits;

  struct glaze {
    using T = SessionLogonResponse;
    static constexpr auto value = glz::object(
      "id", &T::id,
      "status", &T::status,
      "error", &T::error,
      "result", &T::result,
      "rateLimits", &T::rate_limits);
  };
};

}  // namespace futures
}  // namespace schema

#endif  //FUTURES_SESSION_RESPONSE_H
