/*
* MIT License
 *
 * Copyright (c) 2025 NewOro Corporation
 *
 * Permission is hereby granted, free of charge, to use, copy, modify, and
 * distribute this software for any purpose with or without fee, provided that
 * the above copyright notice appears in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 */

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include <mpsc_queue_cas.hpp>
#include <thread.hpp>

TEST(MpscTest, MpscTest) {
  common::MPSCSegQueue<int, static_cast<int>(64)> mpsc_test;

  constexpr int thread_cnt = 500;
  constexpr int value = 1000;

  auto worker = [&]() {
    for (int i =0; i < thread_cnt; i++) {
      mpsc_test.enqueue(value);
    }
  };


  constexpr int N = 100;
  std::vector<std::thread> threads;

  for (int i = 0; i < N; ++i) {
    threads.emplace_back(worker);
  }

  for (auto& t : threads) {
    t.join();
  }

  int temp = 0;
  int total_cnt = 0;
  while (mpsc_test.dequeue(temp)) {
    total_cnt++;
    EXPECT_EQ(temp, value);
  }

  EXPECT_EQ(total_cnt, N * thread_cnt);
}