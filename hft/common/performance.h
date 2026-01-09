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

#ifndef PERFORMANCE_H
#define PERFORMANCE_H

#include <cstdint>

#if defined(__APPLE__)
#include <mach/mach_time.h>
#endif

namespace common {
constexpr unsigned int kShift = 32;

inline auto rdtsc() noexcept {
#if defined(__x86_64__) || defined(_M_X64)
  unsigned int lower_bit;
  unsigned int high_bit;
  __asm__ __volatile__("rdtsc" : "=a"(lower_bit), "=d"(high_bit));
  return (static_cast<uint64_t>(high_bit) << kShift) | lower_bit;
#elif defined(__APPLE__)
  return mach_absolute_time();
#else
  return 0ULL;
#endif
}

inline auto rdtsc_start() noexcept {
#if defined(__x86_64__) || defined(_M_X64)
  unsigned int lower_bit;
  unsigned int high_bit;
  __asm__ __volatile__(
      "lfence\n\t"
      "rdtsc"
      : "=a"(lower_bit), "=d"(high_bit)::);
  return (static_cast<uint64_t>(high_bit) << kShift) | lower_bit;
#elif defined(__APPLE__)
  return mach_absolute_time();
#else
  return 0ULL;
#endif
}

inline auto rdtsc_end() noexcept {
#if defined(__x86_64__) || defined(_M_X64)
  unsigned int lower_bit;
  unsigned int high_bit;
  __asm__ __volatile__("rdtscp" : "=a"(lower_bit), "=d"(high_bit)::"rcx");
  return (static_cast<uint64_t>(high_bit) << kShift) | lower_bit;
#elif defined(__APPLE__)
  return mach_absolute_time();
#else
  return 0ULL;
#endif
}
}  // namespace common

#ifdef MEASUREMENT
#define START_MEASURE(TAG) const auto TAG = common::rdtsc_start()
#define END_MEASURE(TAG, log)                            \
  do {                                                   \
    const auto end = common::rdtsc_end();                \
    (log).fatal("[RDTSC]: {}: {}", #TAG, (end - (TAG))); \
  } while (false)
#else
#define START_MEASURE(TAG) ((void)0)
#define END_MEASURE(TAG, LOG) ((void)0)
#endif

#endif  //PERFORMANCE_H