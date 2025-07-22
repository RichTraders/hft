//
// Created by neworo2 on 25. 7. 18.
//
#include "logger.h"
#include "memory_pool.hpp"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace common;

struct Bin {
  int cnt;
  std::string name;
};

template <typename T>
class MemoryPoolTest {
public:
  MemoryPoolTest(int size, const std::shared_ptr<Logger>& logger): pool_(size) {
    logger_ = logger;
  }

  bool insert(T bin, const uint64_t uid) {
    T* pbin = pool_.allocate(bin);

    if (pbin == nullptr) {
      logger_->info("MemoryPoolTest: Failed to allocate bin");
      return false;
    }

    bins_.emplace(uid, pbin);
    return true;
  }

  bool deallocate(const uint64_t uid) {
    auto pbin = bins_.find(uid);

    if (pbin == bins_.end()) {
      return false;
    }

    bins_.erase(pbin);

    return pool_.deallocate(pbin->second);
  }

private:
  MemoryPool<Bin> pool_;
  std::map<uint64_t, T*> bins_;
  std::shared_ptr<Logger> logger_;
};

TEST(MemoryPoolTest, MemoryPoolFullTest) {
  std::shared_ptr<Logger> lg = std::make_shared<Logger>();
  lg.get()->addSink(std::make_unique<FileSink>("MemoryPoolFullTest", 1024));

  MemoryPoolTest<Bin> test(64, lg);

  Bin temp;
  temp.cnt = 1004;
  temp.name = "test";

  for (int i = 0; i < 65; i++) {
    if (i < 63) {
      EXPECT_TRUE(test.insert(temp, i));
    } else {
      EXPECT_FALSE(test.insert(temp, i));
    }
  }

  for (int i = 0; i < 63; i++) {
    EXPECT_TRUE(test.deallocate(i));
  }
}

TEST(MemoryPoolTest, MemoryPoolTest) {
  std::shared_ptr<Logger> lg = std::make_shared<Logger>();
  lg.get()->addSink(std::make_unique<FileSink>("MemoryPoolTest", 1024));

  MemoryPoolTest<Bin> test(64, lg);

  Bin temp;
  temp.cnt = 1004;
  temp.name = "test";

  for (int i = 0; i < 125; i++) {
    if (i % 2 == 0) {
      EXPECT_TRUE(test.insert(temp, i));
    } else {
      EXPECT_TRUE(test.insert(temp, i));
      EXPECT_TRUE(test.deallocate(i));
    }
  }

  EXPECT_FALSE(test.insert(temp, 444));
}