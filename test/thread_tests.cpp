//
// Created by neworo2 on 25. 7. 8.
//


#include "../hft/common/thread.hpp"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace common;

class ThreadNameTest {
public:
  ThreadNameTest() = default;
  virtual ~ThreadNameTest() = default;

  void worker() {
    std::cout << "worker123123!!!!!!!!" << "\n";
    sleep(2);
  }

  int start() {
    return _thread.start(&ThreadNameTest::worker, this);
  }

  void join() {
    _thread.join();
  }

  int set_thread_name(const std::string& name) {
    return _thread.set_thread_name(name);
  }

  std::string get_thread_name() const {
    return _thread.get_thread_name();
  }

private:
  Thread<NormalTag> _thread;
};

class ThreadNormalTest {
public:
  ThreadNormalTest() = default;
  virtual ~ThreadNormalTest() = default;

  void worker() {
    std::cout << "worker!!!!!!!!" << "\n";
  }

  int start() {
    return _thread.start(&ThreadNormalTest::worker, this);
  }

  int join() {
    return _thread.join();
  }

private:
  Thread<NormalTag> _thread;
};

template<int Priority>
class ThreadPriorityTest {
public:
  ThreadPriorityTest() = default;
  virtual ~ThreadPriorityTest() = default;

  void worker() {
    std::cout << "PThreadPriorityTest!!!!!!!!" << "\n";
    //sleep(5);
  }

  int start() {
    return _thread.start(&ThreadPriorityTest::worker, this);
  }

  int get_priority_level() const {
    return _thread.get_priority_level();
  }

  int join() {
    return _thread.join();
  }

private:
  Thread<PriorityTag<Priority>> _thread;
};

template<int CpuId>
class ThreadAffinityTest {
public:
  ThreadAffinityTest() = default;
  virtual ~ThreadAffinityTest() = default;

  void worker() {
    std::cout << "PThreadAffinityTest worker!!!!!!!!" << "\n";
    sleep(1);
  }

  int start() {
    return _thread.start(&ThreadAffinityTest::worker, this);
  }

  int get_cpu_id() const {
    return _thread.get_cpu_id();
  }

  int join() {
    return _thread.join();
  }

private:
  Thread<AffinityTag<CpuId>> _thread;
};

template<int Priority, int CpuId>
class ThreadPriorityAndAffinityTest {
public:
  ThreadPriorityAndAffinityTest() = default;
  virtual ~ThreadPriorityAndAffinityTest() = default;

  void worker() {
    std::cout << "PThreadPriorityAndAffinityTest worker!!!!!!!!" << "\n";
    sleep(3);
  }

  int start() {
    return _thread.start(&ThreadPriorityAndAffinityTest::worker, this);
  }

  int get_cpu_id() const {
    return _thread.get_cpu_id();
  }

  int get_priority_level() const {
    return _thread.get_priority_level();
  }

  int join() {
    return _thread.join();
  }

private:
  Thread<PriorityTag<Priority>, AffinityTag<CpuId>> _thread;
};

class ThreadJoinTest {
public:
  ThreadJoinTest() = default;
  virtual ~ThreadJoinTest() = default;

   int worker(int temp) {
    std::cout << "PThreadJoinTest!!!!!!!!" << "\n";
    std::cout << "temp = " << temp << "\n";
    std::cout << "_param = " << _param << "\n";

    return temp;
  }

  int start(const int input) {
    return _thread.start(&ThreadJoinTest::worker, this, input);
  }

  int join() {
    return _thread.join();
  }

private:
  Thread<NormalTag> _thread;
  int _param = 1004;
};

TEST(ThreadTest, PriorityTest) {
  constexpr int priority_level = 90;
  ThreadPriorityTest<priority_level> thread;
  EXPECT_EQ(thread.start(), 0);

  EXPECT_EQ(priority_level, thread.get_priority_level());
}

TEST(ThreadTest, AffinityTest) {
  constexpr int cpu_id = 2;
  ThreadAffinityTest<cpu_id> thread;

  EXPECT_EQ(thread.start(), 0);
  int thread_cpu_id = thread.get_cpu_id();
  printf("cpuid: %d\n", thread_cpu_id);

  EXPECT_EQ(cpu_id, thread_cpu_id);
}

TEST(ThreadTest, PriorityAndAffinityTest) {
  constexpr int priority_level = 90;
  constexpr int cpu_id = 2;
  ThreadPriorityAndAffinityTest<priority_level, cpu_id> thread;

  EXPECT_EQ(thread.start(), 0);

  int recv_priority = thread.get_priority_level();
  int recv_cpu_id = thread.get_cpu_id();

  printf("recv_priority: %d, recv_cpu_id: %d\n", recv_priority, recv_cpu_id);

  EXPECT_EQ(priority_level, recv_priority);
  EXPECT_EQ(cpu_id, recv_cpu_id);
}

TEST(ThreadTest, NormalTest) {
  ThreadNormalTest thread;

  EXPECT_EQ(thread.start(), 0);
}

TEST(ThreadTest, ThreadNameTest) {
  ThreadNameTest thread;

  EXPECT_EQ(thread.start(), 0);

  std::string name = "thread_test";

  ASSERT_FALSE(thread.set_thread_name(name));

  std::string recv_name = thread.get_thread_name();

  EXPECT_EQ(name, recv_name);
}

TEST(ThreadTest, ThreadJoinTest) {
  ThreadJoinTest thread;
  constexpr int input = 100;

  EXPECT_EQ(thread.start(input), 0);

  int s = thread.join();

  EXPECT_EQ(s, input);
}