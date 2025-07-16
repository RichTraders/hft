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

namespace common {
constexpr unsigned int kShift = 32;

inline auto rdtsc() noexcept {
  unsigned int lower_bit;
  unsigned int high_bit;
  __asm__ __volatile__("rdtsc" : "=a"(lower_bit), "=d"(high_bit));
  return (static_cast<uint64_t>(high_bit) << kShift) | lower_bit;
}
}  // namespace common

#define START_MEASURE(TAG) const auto TAG = common::rdtsc()
#define END_MEASURE(TAG)                                               \
  do {                                                                 \
    const auto end = common::rdtsc();                                  \
    std::cout << "[RDTSC]: " << #TAG << ": " << (end - (TAG)) << "\n"; \
  } while (false)
#endif  //PERFORMANCE_H