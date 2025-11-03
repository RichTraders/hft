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

#include <barrier>
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace common;
struct Backoff {
  std::mt19937_64 rng{std::random_device{}()};
  std::uniform_int_distribution<int> dist{0, 100};
  void jitter() {
    int r = dist(rng);
    if (r < 60) {
      std::this_thread::yield();
    } else if (r < 95) {
      std::this_thread::sleep_for(std::chrono::microseconds(1));
    } else {
      std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
  }
};

TEST(SPSCQueueTest, SingleThreadBasic) {
  common::SPSCQueue<int, 8> q;  // 유효 용량 8
  EXPECT_TRUE(q.empty());
  EXPECT_FALSE(q.full());

  // 꽉 채우기
  for (int i = 0; i < 8; ++i) {
    EXPECT_TRUE(q.enqueue(i)) << i;
  }
  EXPECT_TRUE(q.full());
  EXPECT_FALSE(q.enqueue(999));

  // 전부 빼기
  for (int i = 0; i < 8; ++i) {
    int v = -1;
    EXPECT_TRUE(q.dequeue(v));
    EXPECT_EQ(v, i);
  }
  EXPECT_TRUE(q.empty());
  int dummy;
  EXPECT_FALSE(q.dequeue(dummy));

  // 여러 번 wrap-around
  for (int cycle = 0; cycle < 10; ++cycle) {
    for (int i = 0; i < 8; ++i)
      EXPECT_TRUE(q.enqueue(i + cycle * 8));
    EXPECT_TRUE(q.full());
    for (int i = 0; i < 8; ++i) {
      int v = -1;
      EXPECT_TRUE(q.dequeue(v));
      EXPECT_EQ(v, i + cycle * 8);
    }
    EXPECT_TRUE(q.empty());
  }
}

// 멀티스레드: 순서/중복/누락 검증 + 타이밍 교란
template <std::size_t Capacity>
void RunSPSCScenario(std::size_t N) {
  common::SPSCQueue<std::size_t, Capacity> q;

  std::vector<std::atomic<int>> seen(N);
  for (auto& a : seen)
    a.store(0, std::memory_order_relaxed);

  std::barrier start(2);
  Backoff pbo, cbo;

  std::thread producer([&] {
    start.arrive_and_wait();
    for (std::size_t i = 0; i < N; ++i) {
      while (!q.enqueue(i)) {
        pbo.jitter();  // full일 때 다양한 타이밍으로 밀어넣기
      }
      if ((i & 0x3FFF) == 0)
        std::this_thread::yield();
    }
  });

  std::vector<std::size_t> out;
  out.reserve(N);

  std::thread consumer([&] {
    start.arrive_and_wait();
    std::size_t v;
    while (out.size() < N) {
      if (q.dequeue(v)) {
        out.push_back(v);
        // 중복/누락 검증용 카운터
        int prev = seen[v].fetch_add(1, std::memory_order_relaxed);
        ASSERT_EQ(prev, 0) << "duplicate value " << v;
      } else {
        cbo.jitter();  // empty일 때 대기
      }
    }
  });

  producer.join();
  consumer.join();

  ASSERT_EQ(out.size(), N);
  // 순서 단조 증가(생산자가 i 순서로 넣었으므로 SPSC면 동일 순서여야 함)
  for (std::size_t i = 0; i < N; ++i) {
    EXPECT_EQ(out[i], i);
  }
  // 정확히 한 번씩만 소비되었는지
  for (std::size_t i = 0; i < N; ++i) {
    EXPECT_EQ(seen[i].load(std::memory_order_relaxed), 1) << "missing " << i;
  }

  // 끝난 뒤 큐 상태 합리성
  EXPECT_TRUE(q.empty());
  EXPECT_FALSE(q.full());
}

TEST(SPSCQueueTest, MultiThread_Stress_SmallCapacity) {
  // capacity 1~4는 경계조건(항상 비우고 채우는 경로) 강하게 때림
  RunSPSCScenario<2>(50'000);
  RunSPSCScenario<4>(50'000);
  RunSPSCScenario<8>(50'000);
  RunSPSCScenario<16>(50'000);
}

TEST(SPSCQueueTest, MultiThread_Stress_MediumCapacity) {
  RunSPSCScenario<64>(200'000);
  RunSPSCScenario<512>(200'000);
}

// 랜덤 타이밍 강화를 위해 여러 번 반복
TEST(SPSCQueueTest, MultiThread_Repetition) {
  for (int rep = 0; rep < 3; ++rep) {
    RunSPSCScenario<128>(100'000);
  }
}

// full/empty 경계가 멀티스레드에서도 일관적인지 빠르게 왕복
TEST(SPSCQueueTest, FullEmptyBoundaryRaces) {
  common::SPSCQueue<int, 2> q;  // 유효 2
  std::barrier start(2);
  std::atomic<bool> running{true};

  std::thread p([&] {
    start.arrive_and_wait();
    int x = 0;
    while (running.load(std::memory_order_relaxed)) {
      if (!q.enqueue(x)) {
        std::this_thread::yield();
      } else {
        x++;
      }
    }
  });

  std::thread c([&] {
    start.arrive_and_wait();
    int v;
    for (int i = 0; i < 100'000; ++i) {
      if (!q.dequeue(v)) {
        std::this_thread::yield();
      }
      // 중간중간 상태 체크
      (void)q.full();
      (void)q.empty();
    }
    running.store(false, std::memory_order_relaxed);
  });

  p.join();
  c.join();

  // 여기까지 오면 hang 없이 정상 종료
  SUCCEED();
}