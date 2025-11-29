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
  std::uint64_t lastUpdateId;
  std::vector<std::array<double, 2>> bids;
  std::vector<std::array<double, 2>> asks;

  struct glaze {
    using T = DepthSnapshotResult;
    static constexpr auto value = glz::object(
        "lastUpdateId", &T::lastUpdateId,
        "bids", glz::quoted_num<&T::bids>,
        "asks", glz::quoted_num<&T::asks>
    );
  };
};

struct DepthSnapshot {
  std::string id;
  int status;
  DepthSnapshotResult result;
  std::vector<RateLimit> rateLimits;

  struct glaze {
    using T = DepthSnapshot;
    static constexpr auto value = glz::object(
        "id", &T::id,
        "status", &T::status,
        "result", &T::result,
        "rateLimits", &T::rateLimits
    );
  };
};
}
#endif  //SNAPSHOT_H
