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

/*
 * cpu를 해당 스레드에만 할당하려면, 다음과 같은 작업을 진행해야함
 *  - 커널 부팅 파라미터 설정
 *  -
 */

#pragma once

#include <pch.h>

namespace common {
class NormalTag {
public:
  static int set_thread_cpu(pthread_t) {
    return 0;
  }
};

template<int PriorityLevel>
class PriorityTag {
public:
  static int set_thread_cpu(pthread_t tid){
    sched_param sch_params;
    sch_params.sched_priority = PriorityLevel;

    int ret = pthread_setschedparam(tid, SCHED_FIFO, &sch_params);

    if (ret != 0) {
      return ret;
    }

    int policy;
    sched_param cur_sch_params;

    ret = pthread_getschedparam(tid, &policy, &cur_sch_params);
    if (ret != 0) {
      return ret;
    }

    if (cur_sch_params.sched_priority != PriorityLevel) {
      return -1;
    }

    return 0;
  }
};

template<int CpuID>
class AffinityTag {

public:
  static constexpr bool is_affinity_with_level(int cpu_id) {
    return cpu_id < 0;
  }

  static int set_thread_cpu(pthread_t tid){
    static_assert(!is_affinity_with_level(CpuID), "cpu_id can't be below 0 in Affinity mode");

    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(CpuID, &mask);

    return pthread_setaffinity_np(tid, sizeof(mask), &mask);
  }
};

template<typename F, typename... Args>
struct ThreadContext  {
  std::decay_t<F> fn;
  std::tuple<std::decay_t<Args>...> args;

  ThreadContext(F&& f, Args&&... a)
    : fn(std::forward<F>(f))
    , args(std::forward<Args>(a)...)
  {}

  void run() {
    std::apply(fn, args);
  }

  static void* entry(void* vp) {
    std::unique_ptr<ThreadContext> ctx(
            static_cast<ThreadContext*>(vp)
        );

    using RetT = std::invoke_result_t<F, Args...>;

    if constexpr (std::is_void_v<RetT>) {
      std::apply(ctx->fn, ctx->args);
      return nullptr;
    }
    else if constexpr (std::is_same_v<RetT, void*>) {
      return std::apply(ctx->fn, ctx->args);
    }
    else if constexpr (std::is_same_v<RetT, int>) {
      int r = std::apply(ctx->fn, ctx->args);
      return new int(r);
    }
    else {
      static_assert(false, "Unsupported thread function return type");
    }
  }
};

template<typename... Tags>
class Thread {
public:
  Thread() = default;
  virtual ~Thread() = default;

  template<typename F, typename... Args>
  int start(F&& fn, Args&&... args) {

    auto* ctx = new ThreadContext<F, Args...>(
        std::forward<F>(fn),
        std::forward<Args>(args)...
    );

    int err = pthread_create(&_tid, nullptr,
                             &ThreadContext<F, Args...>::entry,
                             ctx);
    if (err) {
      delete ctx;
      ctx = nullptr;
      return false;
    }

    return (Tags::set_thread_cpu(_tid) || ...);
  }

  int join() const {
    void *ret = nullptr;

    if (_tid) {
      if (pthread_join(_tid, &ret) !=0)
        return -1;
    }

    if (ret == nullptr)
      return -1;

    std::unique_ptr<int> p(static_cast<int*>(ret));
    return *p;
  }

  int detach() const {
    if (_tid == 0)
      return -1;

    return pthread_detach(_tid);
  }

  int get_priority_level() const {
    int policy;
    sched_param cur_sch_params;

    if (pthread_getschedparam(_tid, &policy, &cur_sch_params)) {
      return -1;
    }

    return cur_sch_params.sched_priority;
  }

  int set_thread_name(const std::string& name) {
    if (_tid == 0)
      return -1;

    return pthread_setname_np(_tid, name.c_str());
  }

  std::string get_thread_name() const {
    if (_tid == 0)
      return "";

    char name[16] = { 0, };

    pthread_getname_np(_tid, name, sizeof(name));
    return std::string(name);
  }

  int get_cpu_id() const {
    int cpu_id = -1;
    cpu_set_t cpuset;

    CPU_ZERO(&cpuset);

    if (pthread_getaffinity_np(_tid, sizeof(cpu_set_t), &cpuset))
      return -1;

    for (int i = 0; i < CPU_SETSIZE; i++) {
      if (CPU_ISSET(i, &cpuset)) {
        cpu_id = i;
        break;
      }
    }

    return cpu_id;
  }

  pthread_t get_thread_id() const {
    return _tid;
  }

private:
  pthread_t _tid{0};
};
}