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

namespace common {
struct WaitStrategy {
  static constexpr int kBusySpinIters = 1'000;  // 완전 바쁜-스핀
  static constexpr int kYieldIters = 5'000;     // sched_yield()
  static constexpr int kSleepIters = 50'000;    // 짧게 sleep

  static constexpr int kUltraShortSleep = 50;

  int iter = 0;

  void idle() {
    if (iter < kBusySpinIters) {
#if defined(__x86_64__) || defined(_M_X64)
      _mm_pause();
#endif
    } else if (iter < kBusySpinIters + kYieldIters) {
      std::this_thread::yield();
    } else if (iter < kBusySpinIters + kYieldIters + kSleepIters) {
      std::this_thread::sleep_for(std::chrono::microseconds(kUltraShortSleep));
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ++iter;
  }

  void reset() { iter = 0; }
};
}  // namespace common

#endif  //WAIT_STRATEGY_H
