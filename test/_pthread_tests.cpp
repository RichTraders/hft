//
// Created by neworo2 on 25. 7. 8.
//


#include "../util/_pthread.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

class PThread_test {
public:
  PThread_test() = default;
  virtual ~PThread_test() = default;

  void set_cpu_id(const int id) {
    thread_.set_cpu_id(id);
  }

  void worker() {
    std::cout << "worker!!!!!!!!" << "\n";
  }

  void start() {
    // 멤버 함수 + 인스턴스 포인터도 그대로 넘길 수 있다
    thread_.start(&PThread_test::worker, this);
  }

  bool check_cpu_id(const int cpu_id) {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    if (pthread_getaffinity_np(thread_.get_thread_id(), sizeof(mask), &mask) != 0) {
      return false;
    }

    if (CPU_ISSET(cpu_id, &mask))
      return true;

    return false;
  }

  void wait() {
    thread_.join();
  }

private:
  PThread thread_;
};

class PThread_test2 {
public:
  PThread_test2() = default;
  virtual ~PThread_test2() = default;

  void set_cpu_id(const int id) {
    thread_.set_cpu_id(id);
  }

  void worker(int a, int b) {
    std::cout << "worker222!!!!!!!" << a << b << "\n";
  }

  void start() {
    // 멤버 함수 + 인스턴스 포인터도 그대로 넘길 수 있다
    thread_.start(&PThread_test2::worker, this,1,2);
  }

  bool check_cpu_id(const int cpu_id) {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    if (pthread_getaffinity_np(thread_.get_thread_id(), sizeof(mask), &mask) != 0) {
      return false;
    }

    if (CPU_ISSET(cpu_id, &mask))
      return true;

    return false;
  }

  void wait() {
    thread_.join();
  }

private:
  PThread thread_;
};


TEST(AllocateThreadToCpuTest, AllocateSingleThread) {
  std::vector<PThread_test> thread;
  std::vector<PThread_test2> thread2;

  int cpu_id_list[4]={3,1,0,2};


  for (int i =0; i<4;i++) {

    if (i<2) {
      thread.emplace_back();
      thread[i].set_cpu_id(cpu_id_list[i]);
      thread[i].start();
    }
    else {
      thread2.emplace_back();
      thread2[i-2].set_cpu_id(cpu_id_list[i]);
      thread2[i-2].start();
    }
  }

  sleep(1);

  for (int i =0; i<4;i++) {
    if (i<2) {
      EXPECT_TRUE(thread[i].check_cpu_id(cpu_id_list[i]));
    }
    else {
      EXPECT_TRUE(thread2[i-2].check_cpu_id(cpu_id_list[i]));
    }
  }
}