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

#ifndef COMMON_THREAD_HPP
#define COMMON_THREAD_HPP

#include <pthread.h>
#include <sched.h>
#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>

#include "global.h"
#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/thread_policy.h>
#endif

namespace common {
template <typename F, typename... Args>
struct ThreadContext {
  std::decay_t<F> fn;
  std::tuple<std::decay_t<Args>...> args;

  explicit ThreadContext(F&& func, Args&&... avrgs)
      : fn(std::forward<F>(func)), args(std::forward<Args>(avrgs)...) {}

  void run() { std::apply(fn, args); }

  static void* entry(void* void_point) {
    std::unique_ptr<ThreadContext> ctx(static_cast<ThreadContext*>(void_point));

    using RetT = std::invoke_result_t<F, Args...>;

    if constexpr (std::is_void_v<RetT>) {
      std::apply(ctx->fn, ctx->args);
      return nullptr;
    } else if constexpr (std::is_same_v<RetT, void*>) {
      return std::apply(ctx->fn, ctx->args);
    } else if constexpr (std::is_same_v<RetT, int>) {
      int ret = std::apply(ctx->fn, ctx->args);
      return new int(ret);
    } else {
      static_assert(false, "Unsupported thread function return type");
    }
  }
};

template <FixedString Name>
class Thread {
 public:
  Thread() = default;
  virtual ~Thread() = default;

  template <typename F, typename... Args>
  int start(F&& func, Args&&... args) {

    auto* ctx = new ThreadContext<F, Args...>(std::forward<F>(func),
        std::forward<Args>(args)...);

    const int err =
        pthread_create(&tid_, nullptr, &ThreadContext<F, Args...>::entry, ctx);
    if (err) {
      delete ctx;
      ctx = nullptr;
      return true;
    }

    set_thread_name(std::string{Name.name});
    return false;
  }
  // NOLINTNEXTLINE(modernize-use-nodiscard)
  [[maybe_unused]] int join() const {
    void* ret = nullptr;

    if (tid_) {
      if (pthread_join(tid_, &ret) != 0)
        return -1;
    }

    if (ret == nullptr)
      return -1;

    const std::unique_ptr<int> pointer(static_cast<int*>(ret));
    return *pointer;
  }

  [[nodiscard]] int detach() const {
#ifdef __APPLE__
    if (tid_ == nullptr)
#else
    if (tid_ == 0)
#endif
      return -1;

    return pthread_detach(tid_);
  }

  [[nodiscard]] int get_priority_level() const {
    int policy;
    sched_param cur_sch_params;

    if (pthread_getschedparam(tid_, &policy, &cur_sch_params)) {
      return -1;
    }

    return cur_sch_params.sched_priority;
  }

  int set_thread_name(const std::string& name) {
#ifdef __APPLE__
    if (tid_ == nullptr)
#else
    if (tid_ == 0)
#endif
      return -1;
#ifdef __APPLE__
    return pthread_setname_np(name.c_str());
#else
    return pthread_setname_np(tid_, name.c_str());
#endif
  }

  [[nodiscard]] std::string get_thread_name() const {
#ifdef __APPLE__
    if (tid_ == nullptr)
#else
    if (tid_ == 0)
#endif
      return "";

    static constexpr std::size_t kMaxThreadNameLen = 16;
    std::array<char, kMaxThreadNameLen> name{};

#ifdef __APPLE__
    if (pthread_equal(tid_, pthread_self())) {
      pthread_getname_np(tid_, name.data(), name.size());
    } else {
      return "";
    }
#else
    pthread_getname_np(tid_, name.data(), name.size());
#endif
    return std::string(name.data());
  }

  [[nodiscard]] int get_cpu_id() const {
#ifdef __linux__
    int cpu_id = -1;
    cpu_set_t cpuset;

    CPU_ZERO(&cpuset);

    if (pthread_getaffinity_np(tid_, sizeof(cpu_set_t), &cpuset))
      return -1;

    for (int i = 0; i < CPU_SETSIZE; i++) {
      if (CPU_ISSET(i, &cpuset)) {
        cpu_id = i;
        break;
      }
    }

    return cpu_id;
#else
    return -1;
#endif
  }

  int set_affinity(int cpu_id) {
#ifdef __APPLE__
    if (tid_ == nullptr)
#else
    if (tid_ == 0)
#endif
      return -1;
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);
    return pthread_setaffinity_np(tid_, sizeof(cpu_set_t), &cpuset);
#elif __APPLE__
    thread_affinity_policy_data_t policy = {cpu_id};
    thread_port_t mach_thread = pthread_mach_thread_np(tid_);
    return thread_policy_set(mach_thread,
        THREAD_AFFINITY_POLICY,
        reinterpret_cast<thread_policy_t>(&policy),
        THREAD_AFFINITY_POLICY_COUNT);
#else
    return 0;  // Not supported
#endif
  }

  [[nodiscard]] pthread_t get_thread_id() const { return tid_; }

 private:
#ifdef __APPLE__
  pthread_t tid_{nullptr};
#else
  pthread_t tid_{0};
#endif
};
}  // namespace common

#endif  // COMMON_THREAD_HPP