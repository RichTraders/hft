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

#include "global.h"

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

    int err =
        pthread_create(&tid_, nullptr, &ThreadContext<F, Args...>::entry, ctx);
    if (err) {
      delete ctx;
      ctx = nullptr;
      return true;
    }

    constexpr std::string_view kName{Name.name, sizeof(Name.name) - 1};
    set_thread_name(kName.data());
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
    if (tid_ == 0)
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
    if (tid_ == 0)
      return -1;

    return pthread_setname_np(tid_, name.c_str());
  }

  [[nodiscard]] std::string get_thread_name() const {
    if (tid_ == 0)
      return "";

    static constexpr std::size_t kMaxThreadNameLen = 16;
    std::array<char, kMaxThreadNameLen> name{};

    pthread_getname_np(tid_, name.data(), name.size());
    return std::string(name.data());
  }

  [[nodiscard]] int get_cpu_id() const {
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
  }

  [[nodiscard]] pthread_t get_thread_id() const { return tid_; }

 private:
  pthread_t tid_{0};
};
}  // namespace common