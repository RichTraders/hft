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
#include <barrier>

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


using namespace std::chrono_literals;

// 테스트용 타입: nothrow moveable
struct Payload {
  uint64_t a, b, c, d;
  Payload() noexcept : a(0), b(0), c(0), d(0) {}
  Payload(uint64_t x) noexcept : a(x), b(x), c(x), d(x) {}
  Payload(Payload&&) noexcept = default;
  Payload& operator=(Payload&&) noexcept = default;
};

void run_stress_for_uaf() {
  // ChunkSize를 극단적으로 작게 해서 청크 churn을 유발
  common::MPSCSegQueue<Payload, /*ChunkSize=*/1> q;

  constexpr int kProducers = 8;
  constexpr int kPerProducerPush = 10'000'00;  // 필요시 더 키워서 재현성↑
  constexpr int kConsumerPolls = kProducers * kPerProducerPush;

  std::atomic<bool> start{false};
  std::atomic<bool> done{false};
  std::barrier sync(kProducers + 1);

  // 소비자: 계속 dequeue. 원본 코드에선 head 청크를 끝내면 즉시 delete하므로 위험.
  std::thread consumer([&] {
    sync.arrive_and_wait();
    start.store(true, std::memory_order_release);

    Payload out;
    int got = 0;

    // 레이스 유도를 위해 가끔 짧은 sleep
    std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<int> jitter(0, 99);

    while (got < kConsumerPolls) {
      if (q.dequeue(out)) {
        ++got;
        if (jitter(rng) < 2) {
          // head가 다음 청크로 넘어가 delete를 자주 유도
          std::this_thread::yield();
        }
      } else {
        // 생산이 몰릴 때 소비가 tail을 쫓아가도록
        std::this_thread::yield();
      }
    }
    done.store(true, std::memory_order_release);
  });

  // 생산자들: enqueue를 빡세게 반복
  std::vector<std::thread> producers;
  producers.reserve(kProducers);
  for (int p = 0; p < kProducers; ++p) {
    producers.emplace_back([&, p] {
      sync.arrive_and_wait();
      // 확률적 레이스 유도를 위한 작은 슬립/패턴
      std::mt19937_64 rng{std::random_device{}() + p};
      std::uniform_int_distribution<int> jitter(0, 199);

      for (int i = 0; i < kPerProducerPush; ++i) {
        // 생산 중간중간 잠깐 멈춰서 "cur=tail.load" 이후 스케줄링 전환 가능성↑
        if ((i % 128) == 0 && jitter(rng) < 50) {
          std::this_thread::yield();
        }
        q.enqueue(Payload((uint64_t(p) << 32) | uint64_t(i)));
      }
    });
  }

  for (auto& t : producers) t.join();
  consumer.join();

  // 여기까지 왔다면 논리상으로는 다 빠져나온 것.
  // 하지만 UAF는 ASan 없이 통과해버릴 수도 있음(무증상).
  // 이 테스트는 "ASan 켠 상태"에서 실행해 UAF를 잡는 게 목적.
  SUCCEED();
}

TEST(MPSCSegQueue_Orig_UAF, Stress_ChunkSize1_8P1C) {
#if defined(__has_feature)
#  if __has_feature(address_sanitizer)
  ASSERT_DEATH(run_stress_for_uaf(), "heap-use-after-free|use-after-free|AddressSanitizer");
#  else
  GTEST_SKIP() << "ASan not enabled; skipping death test.";
#  endif
#else
  // GCC/Clang 공통: 런타임에 환경변수로도 판단 가능하지만 생략
  GTEST_SKIP() << "Compiler feature detection not available.";
#endif
}

TEST(MPSCSegQueue_Orig_Memory, Leak_During_Run) {
  // 짧은 러닝 중에 수 MB 단위로 누수가 생길 수 있음(환경 따라 다름)
  {
    common::MPSCSegQueue<Payload, 1> q;
    std::atomic<bool> stop{false};

    std::thread consumer([&] {
      Payload out;
      while (!stop.load(std::memory_order_acquire)) {
        while (q.dequeue(out)) {}
        std::this_thread::yield();
      }
      while (q.dequeue(out)) {}
    });

    std::vector<std::thread> producers;
    for (int p = 0; p < 8; ++p) {
      producers.emplace_back([&]{
        for (int i = 0; i < 200000; ++i) {
          q.enqueue(Payload(i));
          if ((i & 1023) == 0) std::this_thread::yield();
        }
      });
    }
    for (auto& t : producers) t.join();
    stop.store(true, std::memory_order_release);
    consumer.join();
  }
  // 스코프를 벗어나며 파괴자에서 정리되지만, 런 중에 큰 누수가 관측될 수 있고
  // LSan 설정에 따라 보고될 수도 있음.
  SUCCEED();
}

TEST(QueueLeakCheck, SingleThreadDrain) {
  common::MPSCSegQueue<int, 1> q;
  for (int i=0;i<100000;i++) q.enqueue(i);
  int x;
  while (q.dequeue(x)) {}
  SUCCEED();
}