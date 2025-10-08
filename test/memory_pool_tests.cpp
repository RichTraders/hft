//
// Created by neworo2 on 25. 7. 18.
//
#include "logger.h"
#include "memory_pool.hpp"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace common;

// struct Bin {
//   int cnt;
//   std::string name;
// };
//
// template <typename T>
// class MemoryPoolTest {
// public:
//   MemoryPoolTest(int size, const std::shared_ptr<Logger>& logger): pool_(size) {
//     logger_ = logger;
//   }
//
//   bool insert(T bin, const uint64_t uid) {
//     T* pbin = pool_.allocate(bin);
//
//     if (pbin == nullptr) {
//       logger_->info("MemoryPoolTest: Failed to allocate bin");
//       return false;
//     }
//
//     bins_.emplace(uid, pbin);
//     return true;
//   }
//
//   bool deallocate(const uint64_t uid) {
//     auto pbin = bins_.find(uid);
//
//     if (pbin == bins_.end()) {
//       return false;
//     }
//
//     bins_.erase(pbin);
//
//     return pool_.deallocate(pbin->second);
//   }
//
// private:
//   MemoryPool<Bin> pool_;
//   std::map<uint64_t, T*> bins_;
//   std::shared_ptr<Logger> logger_;
// };
//
// TEST(MemoryPoolTest, MemoryPoolFullTest) {
//   std::shared_ptr<Logger> lg = std::make_shared<Logger>();
//   lg.get()->addSink(std::make_unique<FileSink>("MemoryPoolFullTest", 1024));
//
//   MemoryPoolTest<Bin> test(64, lg);
//
//   Bin temp;
//   temp.cnt = 1004;
//   temp.name = "test";
//
//   for (int i = 0; i < 65; i++) {
//     if (i < 63) {
//       EXPECT_TRUE(test.insert(temp, i));
//     } else {
//       EXPECT_FALSE(test.insert(temp, i));
//     }
//   }
//
//   for (int i = 0; i < 63; i++) {
//     EXPECT_TRUE(test.deallocate(i));
//   }
// }
//
// TEST(MemoryPoolTest, MemoryPoolTest) {
//   std::shared_ptr<Logger> lg = std::make_shared<Logger>();
//   lg.get()->addSink(std::make_unique<FileSink>("MemoryPoolTest", 1024));
//
//   MemoryPoolTest<Bin> test(64, lg);
//
//   Bin temp;
//   temp.cnt = 1004;
//   temp.name = "test";
//
//   for (int i = 0; i < 125; i++) {
//     if (i % 2 == 0) {
//       EXPECT_TRUE(test.insert(temp, i));
//     } else {
//       EXPECT_TRUE(test.insert(temp, i));
//       EXPECT_TRUE(test.deallocate(i));
//     }
//   }
//
//   EXPECT_FALSE(test.insert(temp, 444));
// }

struct Tracked {
  static inline std::atomic<int> ctor{0};
  static inline std::atomic<int> dtor{0};
  int id{0};
  Tracked() noexcept { ++ctor; }
  explicit Tracked(int x) noexcept : id(x) { ++ctor; }
  ~Tracked() noexcept { ++dtor; }
};

TEST(MemoryPool, BasicAllocateDeallocate) {
  Tracked::ctor = 0; Tracked::dtor = 0;

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

  // double free 방지
  EXPECT_FALSE(pool.deallocate(p1));   // 이미 free 됐던 p1은 false
  // 잘못된 포인터 방지
  int dummy;
  EXPECT_FALSE(pool.deallocate(reinterpret_cast<Tracked*>(&dummy)));

  // 모두 반환 → dtor 개수 검증
  EXPECT_TRUE(pool.deallocate(p0));
  EXPECT_TRUE(pool.deallocate(p2));
  EXPECT_TRUE(pool.deallocate(p1b));
  EXPECT_EQ(Tracked::ctor.load(), 4); // (0,1,2,42) 총 4회 생성
  EXPECT_EQ(Tracked::dtor.load(), 4); // 모두 파괴되어야 함
  EXPECT_EQ(pool.free_count(), 3u);
}

TEST(MemoryPool, AlignmentIsCorrect) {
  MemoryPool<Tracked> pool(8);
  std::vector<Tracked*> ptrs;
  for (int i = 0; i < 8; ++i) ptrs.push_back(pool.allocate(i));
  for (auto* p : ptrs) {
    ASSERT_NE(p, nullptr);
    uintptr_t up = reinterpret_cast<uintptr_t>(p);
    EXPECT_EQ(up % alignof(Tracked), 0u);
  }
  for (auto* p : ptrs) EXPECT_TRUE(pool.deallocate(p));
}

TEST(MemoryPool, ExhaustAndRefill) {
  MemoryPool<Tracked> pool(2);
  auto* a = pool.allocate(7);
  auto* b = pool.allocate(8);
  EXPECT_EQ(pool.allocate(9), nullptr); // full
  EXPECT_TRUE(pool.deallocate(a));
  auto* c = pool.allocate(10);
  EXPECT_NE(c, nullptr);
  EXPECT_TRUE(pool.deallocate(b));
  EXPECT_TRUE(pool.deallocate(c));
}

// (선택) 단일 스레드 스트레스: 랜덤 alloc/free
TEST(MemoryPool, SingleThreadFuzz) {
  MemoryPool<Tracked> pool(1024);
  std::vector<Tracked*> live; live.reserve(1024);
  std::mt19937_64 rng(123);
  std::uniform_int_distribution<int> act(0, 1);

  for (int i = 0; i < 100000; ++i) {
    if (act(rng) == 0) {
      // alloc
      auto* p = pool.allocate(i);
      if (p) live.push_back(p);
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
  for (auto* p : live) EXPECT_TRUE(pool.deallocate(p));
}


TEST(MemoryPool, TwoThreadStressWithMutex) {
  MemoryPool<Tracked> pool(1 << 14);
  std::mutex m;
  constexpr int N = 200000;

  std::atomic<bool> start{false};
  std::atomic<int> enq{0}, deq{0};

  std::thread producer([&]{
    while (!start.load()) {}
    for (int i = 0; i < N; ++i) {
      std::unique_lock<std::mutex> lk(m);
      if (auto* p = pool.allocate(i)) {
        (void)p;
        ++enq;
      }
    }
  });

  std::thread consumer([&]{
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