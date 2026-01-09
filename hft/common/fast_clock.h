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

#ifndef FAST_CLOCK_H
#define FAST_CLOCK_H

#include <chrono>
#include <cmath>
#include <cstdint>

#include "performance.h"
#include "types.h"

namespace common {
constexpr double kGhz = 1e9;
constexpr double kHourToSeconds = 3600;
struct FastClock {
  const uint64_t recal_cycles;

  double inv_f;
  uint64_t last_cycle;
  uint64_t last_epoch;

  FastClock(double cpu_hz, unsigned interval_sec)
      : recal_cycles(static_cast<uint64_t>(cpu_hz * interval_sec)),
        inv_f(kGhz / cpu_hz),
        last_cycle(rdtsc()),
        last_epoch(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch())
                       .count()) {}

  uint64_t get_timestamp() noexcept {
    const uint64_t current_cycle = rdtsc();
    uint64_t cycle_diff = current_cycle - last_cycle;

    if (UNLIKELY(cycle_diff >= recal_cycles)) {
      last_cycle = rdtsc();
      last_epoch = std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::system_clock::now().time_since_epoch())
                       .count();
      cycle_diff = 0;
    }

    // NOLINTNEXTLINE(bugprone-narrowing-conversions)
    const auto dt_ns = static_cast<uint64_t>(inv_f * cycle_diff);
    return last_epoch + dt_ns;
  }
};
}  // namespace common
#endif  //FAST_CLOCK_H