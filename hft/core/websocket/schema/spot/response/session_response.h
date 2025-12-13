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

#ifndef SESSION_RESPONSE_H
#define SESSION_RESPONSE_H
#include <glaze/glaze.hpp>

#include "api_response.h"
namespace schema {
struct SessionUserSubscriptionResponse {
  struct Result {
    std::int32_t subscription_id;

    // clang-format off
    struct glaze {
      using T = Result;
      static constexpr auto value = glz::object(
        "subscriptionId", &T::subscription_id
      );
    };
    // clang-format on
  };

  std::string id;
  std::uint32_t status;
  std::optional<ErrorResponse<Result>> error;
  std::optional<std::vector<RateLimit>> rate_limit;

  std::optional<Result> result;

  // clang-format off
  struct glaze {
    using T = SessionUserSubscriptionResponse;
    static constexpr auto value = glz::object(
          "id", &T::id,
          "status", &T::status,
          "rateLimits", &T::rate_limit,
          "result", &T::result,
          "error",&T::error);
  };
  // clang-format on
};

struct SessionUserUnsubscriptionResponse {
  std::string id;
  std::uint32_t status;
  std::optional<std::vector<RateLimit>> rate_limit;

  struct Result {
    // clang-format off
    struct glaze {
      using T = Result;
      static constexpr auto value = glz::object(
      );
    };
    // clang-format on
  };

  std::optional<Result> result;

  // clang-format off
  struct glaze {
    using T = SessionUserUnsubscriptionResponse;
    static constexpr auto value = glz::object(
          "id", &T::id,
          "status", &T::status,
          "rateLimits", &T::rate_limit,
          "result", &T::result);
  };
  // clang-format on
};

struct SessionLogonResponse {
  std::string id;
  int status{0};
  std::optional<ErrorResponse<>> error;

  struct Result {
    std::string api_key;
    std::uint64_t authorized_since;
    std::uint64_t connected_since;
    bool return_rate_limits;
    std::uint64_t server_time;
    bool user_data_stream;

    // clang-format off
    struct glaze {
      using T = Result;
      static constexpr auto value = glz::object(
        "apiKey", &T::api_key,
        "authorizedSince", &T::authorized_since,
        "connectedSince", &T::connected_since,
        "returnRateLimits", &T::return_rate_limits,
        "serverTime",&T::server_time,
        "userDataStream",&T::user_data_stream
      );
    };
    // clang-format on
  };

  std::optional<Result> result;
  std::optional<std::vector<RateLimit>> rate_limits;

  // clang-format off
  struct glaze {
    using T = SessionLogonResponse;
    static constexpr auto value = glz::object(
      "id", &T::id,
      "status", &T::status,
      "error", &T::error,
      "result", &T::result,
      "rateLimits", &T::rate_limits);
  };
  // clang-format on
};
}  // namespace schema

#endif  //SESSION_RESPONSE_H
