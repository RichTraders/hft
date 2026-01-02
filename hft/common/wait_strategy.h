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

#ifndef WAIT_STRATEGY_H
#define WAIT_STRATEGY_H

#include <chrono>
#include <thread>

#if defined(__x86_64__) || defined(_M_X64)
#include <emmintrin.h>
#endif

namespace common {
struct WaitStrategy {
  static constexpr int kBusySpinIters = 1'000;  // 완전 바쁜-스핀
  static constexpr int kSpinIters = 16'000;
  static constexpr int kYieldIters = 5'000;
  static constexpr int kNsShort = 20'000;    // 20 µs
  static constexpr int kNsLong = 1'000'000;  // 1 ms

  static constexpr int kUltraShortSleep = 50;

  int iter = 0;

  void idle() {
    if (iter < kBusySpinIters) {
#if defined(__x86_64__) || defined(_M_X64)
      _mm_pause();
#endif
    } else if (iter < kBusySpinIters + kYieldIters)
      std::this_thread::sleep_for(std::chrono::nanoseconds(kNsShort));
    else {
      std::this_thread::sleep_for(std::chrono::nanoseconds(kNsLong));
    }
    ++iter;
  }

  void idle_hot() {
#if defined(__x86_64__) || defined(_M_X64)
    _mm_pause();
#elif defined(__aarch64__)
    __asm__ __volatile__("yield");
#endif
    if (++iter > kSpinIters)
      iter = kSpinIters;
  }

  void reset() { iter = 0; }
};
}  // namespace common

#endif  //WAIT_STRATEGY_H
