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

#include <pthread.h>
#include <pch.h>

#pragma once

// Callable + 인자들을 캡처하는 컨텍스트
struct PThreadContextBase {
  virtual ~PThreadContextBase() = default;
  virtual void run() = 0;
};

template<typename F, typename... Args>
struct PThreadContext : PThreadContextBase {
  std::decay_t<F> fn;
  std::tuple<std::decay_t<Args>...> args;

  PThreadContext(F&& f, Args&&... a)
    : fn(std::forward<F>(f))
    , args(std::forward<Args>(a)...)
  {}

  void run() override {
    std::apply(fn, args);
  }

  static void* entry(void* vp) {
    std::unique_ptr<PThreadContext> ctx(static_cast<PThreadContext*>(vp));
    ctx->run();
    return nullptr;
  }
};

class PThread {
public:
  PThread() = default;
  virtual ~PThread() = default;

  void set_cpu_id(const int id) {
    _cpu_id = id;
  }

  pthread_t get_thread_id() const {
    return tid_;
  }

  // 임의의 Callable + 인자 지원
  template<typename F, typename... Args>
  void start(F&& fn, Args&&... args) {
    // 컨텍스트를 heap에 올려서 pthread에 넘김
    auto* ctx = new PThreadContext<F, Args...>(
        std::forward<F>(fn),
        std::forward<Args>(args)...
    );
    int err = pthread_create(&tid_, nullptr,
                             &PThreadContext<F, Args...>::entry,
                             ctx);
    if (err) {
      delete ctx;
      throw std::system_error(err, std::generic_category(),
                              "pthread_create failed");
    }

    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(_cpu_id, &mask);  // i번 스레드는 CPU (i % num_cpus)에 고정
    if (pthread_setaffinity_np(tid_, sizeof(mask), &mask) !=0)
      throw std::system_error(err, std::generic_category(),"pthread_create failed to allocate cpu");

  }
  void join() {
    if (tid_) pthread_join(tid_, nullptr);
  }

private:
  int _cpu_id;
  pthread_t tid_{0};
};

