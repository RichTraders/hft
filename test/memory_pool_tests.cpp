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
#include "logger.h"
#include "memory_pool.hpp"

using namespace common;

struct Tracked {
  static inline std::atomic<int> ctor{0};
  static inline std::atomic<int> dtor{0};
  int id{0};
  Tracked() noexcept { ++ctor; }
  explicit Tracked(int x) noexcept : id(x) { ++ctor; }
  ~Tracked() noexcept { ++dtor; }
};

TEST(MemoryPool, BasicAllocateDeallocate) {
  Tracked::ctor = 0;
  Tracked::dtor = 0;

  MemoryPool<Tracked> pool(3);
  EXPECT_EQ(pool.capacity(), 3u);
  EXPECT_EQ(pool.free_count(), 3u);

  auto* p0 = pool.allocate(0);
  auto* p1 = pool.allocate(1);
  auto* p2 = pool.allocate(2);
  EXPECT_NE(p0, nullptr);
  EXPECT_NE(p1, nullptr);
  EXPECT_NE(p2, nullptr);
  EXPECT_EQ(pool.free_count(), 0u);
  EXPECT_EQ(p0->id, 0);
  EXPECT_EQ(p1->id, 1);
  EXPECT_EQ(p2->id, 2);

  // 풀 가득: 다음 할당은 nullptr
  auto* p3 = pool.allocate(3);
  EXPECT_EQ(p3, nullptr);

  // deallocate 정상 동작
  EXPECT_TRUE(pool.deallocate(p1));
  EXPECT_EQ(pool.free_count(), 1u);

  // 재할당: 다시 p1 자리를 재사용(주소가 같을 수도 있음)
  auto* p1b = pool.allocate(42);
  EXPECT_NE(p1b, nullptr);
  EXPECT_EQ(p1b->id, 42);
  EXPECT_EQ(pool.free_count(), 0u);

  if (p1b == p1) {
    // 같은 슬롯 재사용: p1은 현재 live 객체의 포인터
    EXPECT_TRUE(pool.deallocate(p1));    // 첫 해제 → true
    EXPECT_FALSE(pool.deallocate(p1b));  // 두 번째 해제 → false
  } else {
    // 다른 슬롯: p1은 이미 free 상태였음
    EXPECT_FALSE(pool.deallocate(p1));  // 이미 free → false
    EXPECT_TRUE(pool.deallocate(p1b));  // 정상 해제 → true
  }

  int dummy = 0;
  EXPECT_FALSE(pool.deallocate(reinterpret_cast<Tracked*>(&dummy)));

  // 모두 반환 → dtor 개수 검증
  EXPECT_TRUE(pool.deallocate(p0));
  EXPECT_TRUE(pool.deallocate(p2));
  if (p1b == p1) {
    EXPECT_FALSE(pool.deallocate(p1b));
  } else {
    EXPECT_TRUE(pool.deallocate(p1b));
  }

  EXPECT_EQ(Tracked::ctor.load(), 4);  // (0,1,2,42) 총 4회 생성
  EXPECT_EQ(Tracked::dtor.load(), 4);  // 모두 파괴되어야 함
  EXPECT_EQ(pool.free_count(), 3u);
}

TEST(MemoryPool, AlignmentIsCorrect) {
  MemoryPool<Tracked> pool(8);
  std::vector<Tracked*> ptrs;
  for (int i = 0; i < 8; ++i)
    ptrs.push_back(pool.allocate(i));
  for (auto* p : ptrs) {
    ASSERT_NE(p, nullptr);
    uintptr_t up = reinterpret_cast<uintptr_t>(p);
    EXPECT_EQ(up % alignof(Tracked), 0u);
  }
  for (auto* p : ptrs)
    EXPECT_TRUE(pool.deallocate(p));
}

TEST(MemoryPool, ExhaustAndRefill) {
  MemoryPool<Tracked> pool(2);
  auto* a = pool.allocate(7);
  auto* b = pool.allocate(8);
  EXPECT_EQ(pool.allocate(9), nullptr);  // full
  EXPECT_TRUE(pool.deallocate(a));
  auto* c = pool.allocate(10);
  EXPECT_NE(c, nullptr);
  EXPECT_TRUE(pool.deallocate(b));
  EXPECT_TRUE(pool.deallocate(c));
}

// (선택) 단일 스레드 스트레스: 랜덤 alloc/free
TEST(MemoryPool, SingleThreadFuzz) {
  MemoryPool<Tracked> pool(1024);
  std::vector<Tracked*> live;
  live.reserve(1024);
  std::mt19937_64 rng(123);
  std::uniform_int_distribution<int> act(0, 1);

  for (int i = 0; i < 100000; ++i) {
    if (act(rng) == 0) {
      // alloc
      auto* p = pool.allocate(i);
      if (p)
        live.push_back(p);
    } else if (!live.empty()) {
      // free
      size_t idx = static_cast<size_t>(rng() % live.size());
      auto* p = live[idx];
      EXPECT_TRUE(pool.deallocate(p));
      live[idx] = live.back();
      live.pop_back();
    }
  }
  // 정리
  for (auto* p : live)
    EXPECT_TRUE(pool.deallocate(p));
}

TEST(MemoryPool, TwoThreadStressWithMutex) {
  MemoryPool<Tracked> pool(1 << 14);
  std::mutex m;
  constexpr int N = 200000;

  std::atomic<bool> start{false};
  std::atomic<int> enq{0}, deq{0};

  std::thread producer([&] {
    while (!start.load()) {}
    for (int i = 0; i < N; ++i) {
      std::unique_lock<std::mutex> lk(m);
      if (auto* p = pool.allocate(i)) {
        (void)p;
        ++enq;
      }
    }
  });

  std::thread consumer([&] {
    while (!start.load()) {}
    int local = 0;
    while (local < N) {
      std::unique_lock<std::mutex> lk(m);
      // 간단히 풀 전체를 훑는 대신, 성공 가능성이 높은 반복 free
      // 실제로는 live 목록이 있어야 하지만 여기선 구조 검증이 목적
      // → 아래는 데모용 (실사용에선 별도 구조로 live 관리)
      ++local;
      ++deq;
    }
  });

  start = true;
  producer.join();
  consumer.join();

  // 구조적 에러 없이 종료되는지만 확인
  SUCCEED();
}

// =============================================================================
// Thread Safety Tests - Demonstrating Current MemoryPool Issues
// =============================================================================

// Test 1: Concurrent allocate/deallocate without mutex (WILL FAIL)
TEST(MemoryPoolThreadSafety, ConcurrentAllocateDeallocate) {
  MemoryPool<Tracked> pool(10000);
  constexpr int kThreads = 4;
  constexpr int kOpsPerThread = 10000;
  std::atomic<int> failures{0};

  auto worker = [&]() {
    for (int i = 0; i < kOpsPerThread; ++i) {
      auto* ptr = pool.allocate(i);
      if (ptr == nullptr) {
        ++failures;
        continue;
      }
      // Use the pointer briefly
      volatile int x = ptr->id;
      (void)x;

      if (!pool.deallocate(ptr)) {
        ++failures;
      }
    }
  };

  std::vector<std::thread> threads;
  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back(worker);
  }

  for (auto& t : threads) {
    t.join();
  }

  std::cout << "Concurrent test: " << failures.load() << " failures detected\n";
  std::cout << "Pool state: " << pool.free_count() << " free slots\n";
}

// Test 2: Producer-Consumer pattern (Should be SAFE with current SPSC usage)
TEST(MemoryPoolThreadSafety, ProducerConsumerSPSC) {
  MemoryPool<Tracked> pool(1000);
  constexpr int kMessages = 10000;
  std::vector<Tracked*> queue;
  std::mutex queue_mutex;
  std::atomic<bool> done{false};
  std::atomic<int> allocated{0};
  std::atomic<int> deallocated{0};

  // Producer thread (like MDRead)
  std::thread producer([&]() {
    for (int i = 0; i < kMessages; ++i) {
      auto* ptr = pool.allocate(i);
      if (ptr) {
        ++allocated;
        std::lock_guard lock(queue_mutex);
        queue.push_back(ptr);
      }
    }
    done = true;
  });

  // Consumer thread (like TradeEngine)
  std::thread consumer([&]() {
    while (!done.load() || !queue.empty()) {
      Tracked* ptr = nullptr;
      {
        std::lock_guard lock(queue_mutex);
        if (!queue.empty()) {
          ptr = queue.back();
          queue.pop_back();
        }
      }

      if (ptr) {
        volatile int x = ptr->id;
        (void)x;
        if (pool.deallocate(ptr)) {
          ++deallocated;
        }
      } else {
        std::this_thread::sleep_for(std::chrono::microseconds(10));
      }
    }
  });

  producer.join();
  consumer.join();

  std::cout << "SPSC test: allocated=" << allocated.load()
            << ", deallocated=" << deallocated.load() << "\n";
  EXPECT_EQ(allocated.load(), deallocated.load());
}

// Test 3: Producer-Consumer with ERROR PATH (WILL SHOW UAF)
TEST(MemoryPoolThreadSafety, ProducerConsumerWithErrorPath) {
  MemoryPool<Tracked> pool(1000);
  constexpr int kMessages = 5000;
  std::vector<Tracked*> queue;
  std::mutex queue_mutex;
  std::atomic<bool> done{false};
  std::atomic<int> double_free_detected{0};
  std::atomic<int> allocated{0};
  std::atomic<int> deallocated{0};

  // Producer thread with error path simulation
  std::thread producer([&]() {
    for (int i = 0; i < kMessages; ++i) {
      auto* ptr = pool.allocate(i);
      if (ptr) {
        ++allocated;

        // Simulate error path (10% chance)
        if (i % 10 == 0) {
          // ERROR PATH: Deallocate before enqueueing (ownership violation!)
          if (!pool.deallocate(ptr)) {
            ++double_free_detected;
          } else {
            ++deallocated;
          }
          continue; // Don't enqueue
        }

        std::lock_guard lock(queue_mutex);
        queue.push_back(ptr);
      }
    }
    done = true;
  });

  // Consumer thread
  std::thread consumer([&]() {
    while (!done.load() || !queue.empty()) {
      Tracked* ptr = nullptr;
      {
        std::lock_guard lock(queue_mutex);
        if (!queue.empty()) {
          ptr = queue.back();
          queue.pop_back();
        }
      }

      if (ptr) {
        // This might deallocate already-freed memory!
        if (!pool.deallocate(ptr)) {
          ++double_free_detected;
        } else {
          ++deallocated;
        }
      } else {
        std::this_thread::sleep_for(std::chrono::microseconds(10));
      }
    }
  });

  producer.join();
  consumer.join();

  std::cout << "Error path test: allocated=" << allocated.load()
            << ", deallocated=" << deallocated.load()
            << ", double_free_detected=" << double_free_detected.load() << "\n";
  std::cout << "Pool free count: " << pool.free_count() << "\n";
}

// Test 4: Concurrent Deallocate (CRITICAL - Will show vector corruption)
TEST(MemoryPoolThreadSafety, ConcurrentDeallocateSamePointer) {
  MemoryPool<Tracked> pool(100);
  std::vector<Tracked*> allocated;

  // Pre-allocate objects
  for (int i = 0; i < 100; ++i) {
    allocated.push_back(pool.allocate(i));
  }

  std::atomic<int> success_count{0};
  std::atomic<int> failure_count{0};

  // Try to deallocate each pointer from multiple threads simultaneously
  auto worker = [&](int start, int end) {
    for (int i = start; i < end; ++i) {
      // Multiple threads try to free the same pointer
      if (pool.deallocate(allocated[i])) {
        ++success_count;
      } else {
        ++failure_count;
      }
    }
  };

  constexpr int kThreads = 4;
  std::vector<std::thread> threads;

  // All threads try to deallocate all pointers (should cause chaos)
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back(worker, 0, 100);
  }

  for (auto& t : threads) {
    t.join();
  }

  std::cout << "Concurrent deallocate test:\n";
  std::cout << "  Success: " << success_count.load() << "\n";
  std::cout << "  Failure: " << failure_count.load() << "\n";
  std::cout << "  Expected success: 100 (one per object)\n";
  std::cout << "  Pool free count: " << pool.free_count() << "\n";
}

// Test 5: Vector Corruption Test (Stress test on free_ vector)
TEST(MemoryPoolThreadSafety, VectorCorruptionStressTest) {
  MemoryPool<Tracked> pool(10000);
  constexpr int kThreads = 8;
  constexpr int kAllocationsPerThread = 5000;

  std::vector<std::vector<Tracked*>> thread_allocations(kThreads);
  std::atomic<int> corruption_detected{0};

  // Phase 1: Each thread allocates its own objects
  {
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
      threads.emplace_back([&, t]() {
        for (int i = 0; i < kAllocationsPerThread; ++i) {
          auto* ptr = pool.allocate(t * 10000 + i);
          if (ptr) {
            thread_allocations[t].push_back(ptr);
          }
        }
      });
    }
    for (auto& t : threads) {
      t.join();
    }
  }

  std::cout << "Allocated objects across " << kThreads << " threads\n";

  // Phase 2: All threads deallocate concurrently (NO MUTEX)
  // This should cause vector corruption in free_ vector
  {
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
      threads.emplace_back([&, t]() {
        for (auto* ptr : thread_allocations[t]) {
          if (!pool.deallocate(ptr)) {
            ++corruption_detected;
          }
        }
      });
    }
    for (auto& t : threads) {
      t.join();
    }
  }

  std::cout << "Vector corruption test:\n";
  std::cout << "  Corruption detected: " << corruption_detected.load() << "\n";
  std::cout << "  Pool free count: " << pool.free_count() << "\n";
  std::cout << "  Expected free count: 10000\n";

  // The free count might be wrong due to corruption
  if (pool.free_count() != 10000) {
    std::cout << "  WARNING: Free list corrupted! Count mismatch.\n";
  }
}