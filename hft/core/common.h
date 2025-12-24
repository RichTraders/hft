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

#ifndef COMMON_H
#define COMMON_H

#include <cstdint>
#include <ctime>

namespace util {
constexpr int kMilliseconds = 1000;
constexpr int kNanoseconds = 1'000'000;
inline auto get_timestamp_epoch() {
  timespec time;
  clock_gettime(CLOCK_REALTIME, &time);
  return static_cast<std::uint64_t>(time.tv_sec) * kMilliseconds +
         static_cast<std::uint64_t>(time.tv_nsec) / kNanoseconds;
}
}  // namespace util
#endif  //COMMON_H
