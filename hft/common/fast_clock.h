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
#include <mutex>

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
  std::mutex mtx;

  FastClock(double cpu_hz, unsigned interval_h)
      : recal_cycles(
            static_cast<uint64_t>(cpu_hz * kHourToSeconds * interval_h)),
        inv_f(kGhz / cpu_hz) {
    const std::lock_guard lock_guard(mtx);
    last_cycle = rdtsc();
    last_epoch = std::chrono::duration_cast<std::chrono::nanoseconds>(
                     std::chrono::system_clock::now().time_since_epoch())
                     .count();
  }

  uint64_t get_timestamp() {
    const uint64_t current_cycle = rdtsc();
    uint64_t cycle_diff = current_cycle - last_cycle;

    if (UNLIKELY(cycle_diff >= recal_cycles)) {
      const std::lock_guard lock_guard(mtx);
      last_cycle = rdtsc();
      last_epoch = std::chrono::duration_cast<std::chrono::nanoseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
      cycle_diff = 0;
    }

    const auto dt_ns = std::llround(
        inv_f * cycle_diff);  //NOLINT(bugprone-narrowing-conversions)
    return last_epoch + dt_ns;
  }
};
}  // namespace common
#endif  //FAST_CLOCK_H