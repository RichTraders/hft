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

#ifndef SNAPSHOT_H
#define SNAPSHOT_H

#include <glaze/glaze.hpp>
#include "api_response.h"

namespace schema {
struct DepthSnapshotResult {
  std::uint64_t last_update_id;
  std::vector<std::array<double, 2>> bids;
  std::vector<std::array<double, 2>> asks;

  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = DepthSnapshotResult;
    static constexpr auto value = glz::object(  // NOLINT(readability-identifier-naming)
        "lastUpdateId", &T::last_update_id,
        "bids", glz::quoted_num<&T::bids>,
        "asks", glz::quoted_num<&T::asks>
    );
  };
};

struct DepthSnapshot {
  std::string id;
  int status;
  DepthSnapshotResult result;
  std::vector<RateLimit> rate_limits;

  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = DepthSnapshot;
    static constexpr auto value = glz::object(  // NOLINT(readability-identifier-naming)
        "id", &T::id,
        "status", &T::status,
        "result", &T::result,
        "rateLimits", &T::rate_limits
    );
  };
};
}
}  // namespace schema
#endif  //SNAPSHOT_H
