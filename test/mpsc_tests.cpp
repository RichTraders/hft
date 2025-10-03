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

#include <mpsc_queue_cas.hpp>
#include <thread.hpp>
#include "gmock/gmock.h"
#include "gtest/gtest.h"

TEST(MpscTest, MpscTest) {
  common::MPSCSegQueue<int, static_cast<int>(64)> mpsc_test;

  constexpr int thread_cnt = 500;
  constexpr int value = 1000;

  auto worker = [&]() {
    for (int i = 0; i < thread_cnt; i++) {
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

using namespace std::chrono_literals;
struct Probe {
  int value = -1;
  std::atomic<bool>* block_until = nullptr;    // true 전까지 쓰기 금지
  std::atomic<bool>* entered     = nullptr;    // "쓰기 직전" 진입 신호

  Probe() = default;
  explicit Probe(int v) : value(v) {}
  Probe(int v, std::atomic<bool>* block, std::atomic<bool>* ent)
      : value(v), block_until(block), entered(ent) {}

  Probe(Probe&& other) noexcept
      : value(other.value), block_until(other.block_until), entered(other.entered) {
    if (entered) entered->store(true, std::memory_order_release);
    if (block_until) {
      while (!block_until->load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
    }
  }

  // 혹시 복사 경로로 생성될 수도 있으니 동일 훅
  Probe(const Probe& other)
      : value(other.value), block_until(other.block_until), entered(other.entered) {
    if (entered) entered->store(true, std::memory_order_release);
    if (block_until) {
      while (!block_until->load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
    }
  }
  Probe& operator=(const Probe&) = default;
  Probe& operator=(Probe&&) = default;
};

using namespace std::chrono_literals;

TEST(MPSCQueueCorrectness, ConsumerMustNotSeeUnpublishedSlot) {
  common::MPSCSegQueue<Probe, 2> q;

  std::atomic entered{false};
  std::atomic allow_write{false};

  // P1: slot-write 직전에서 훅으로 멈춤
  std::thread p1([&]{
    q.enqueue(Probe{10, &allow_write, &entered});
  });

  // P1이 쓰기 직전에 진입했는지 대기 (= idx는 이미 증가)
  auto t0 = std::chrono::steady_clock::now();
  while (!entered.load(std::memory_order_acquire) &&
         std::chrono::steady_clock::now() - t0 < 1s) {
    std::this_thread::yield();
         }
  ASSERT_TRUE(entered.load()) << "P1 진입 실패";

  // 1) 아직 publish(쓰기) 전이므로, 이 구간에서는 dequeue가 절대 성공하면 안 됨
  Probe out{};
  bool dequeued_while_blocked = false;
  for (int i = 0; i < 50000; ++i) { // 짧게 여러 번 시도
    if (q.dequeue(out)) { dequeued_while_blocked = true; break; }
    std::this_thread::yield();
  }
  EXPECT_FALSE(dequeued_while_blocked)
      << "버그: publish 전 슬롯을 소비자가 읽음(구멍 슬롯)";

  // 2) 이제 쓰기 허용 → 이후엔 dequeue가 성공해야 정상
  allow_write.store(true, std::memory_order_release);
  p1.join();

  bool got_after_publish = false;
  for (int i = 0; i < 200000; ++i) {
    if (q.dequeue(out)) { got_after_publish = true; break; }
    std::this_thread::yield();
  }
  ASSERT_TRUE(got_after_publish) << "publish 후에도 dequeue가 안 됨";

  // 3) 첫 값이 디폴트(-1)이면 여전히 버그 (미작성 슬롯을 읽은 것)
  EXPECT_NE(out.value, -1) << "버그: 미작성 슬롯(default)을 소비함";
}
