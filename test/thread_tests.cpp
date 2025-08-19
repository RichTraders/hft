//
// Created by neworo2 on 25. 7. 8.
//


#include "thread.hpp"
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
  Thread<"Normal"> _thread;
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
  Thread<"Normal"> _thread;
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
  Thread<"Normal"> _thread;
  int _param = 1004;
};

TEST(ThreadTest, NormalTest) {
  ThreadNormalTest thread;

  EXPECT_TRUE(thread.start());
}

TEST(ThreadTest, ThreadNameTest) {
  ThreadNameTest thread;

  EXPECT_TRUE(thread.start());

  std::string name = "thread_test";

  ASSERT_FALSE(thread.set_thread_name(name));

  std::string recv_name = thread.get_thread_name();

  EXPECT_EQ(name, recv_name);
}

TEST(ThreadTest, ThreadJoinTest) {
  ThreadJoinTest thread;
  constexpr int input = 100;

  EXPECT_TRUE(thread.start(input));

  int s = thread.join();

  EXPECT_EQ(s, input);
}