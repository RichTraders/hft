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

#ifndef DEPTH_SNAPSHOT_H
#define DEPTH_SNAPSHOT_H
#include <glaze/glaze.hpp>
#include "api_response.h"

namespace schema::futures {
struct DepthSnapshotResult {
  std::uint64_t book_update_id;
  std::uint64_t message_output_time;
  std::uint64_t transaction_time;
  std::vector<std::array<double, 2>> bids;
  std::vector<std::array<double, 2>> asks;

  // clang-format off
  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = DepthSnapshotResult;
    static constexpr auto value = glz::object(  // NOLINT(readability-identifier-naming)  // NOLINT(readability-identifier-naming)
        "lastUpdateId", &T::book_update_id,
        "E", &T::message_output_time,
        "T",&T::transaction_time,
        "bids", glz::quoted_num<&T::bids>,
        "asks", glz::quoted_num<&T::asks>
    );
  };
  // clang-format on
};

struct DepthSnapshot {
  std::string id;
  int status;
  DepthSnapshotResult result;
  std::optional<std::vector<RateLimit>> rateLimits;

  // clang-format off
  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = DepthSnapshot;
    static constexpr auto value = glz::object(  // NOLINT(readability-identifier-naming)  // NOLINT(readability-identifier-naming)
        "id", &T::id,
        "status", &T::status,
        "result", &T::result,
        "rateLimits", &T::rateLimits
        );
  };
  // clang-format on
};
}  // namespace schema::futures
#endif //DEPTH_SNAPSHOT_H