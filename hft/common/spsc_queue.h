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

#ifndef SPSCQUEUE_H
#define SPSCQUEUE_H

#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <thread>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386) || \
    defined(_M_IX86)
#include <emmintrin.h>
#endif

namespace common {
constexpr std::size_t kCachelineSize = 64;

template <typename T>
concept SPSCQueueElement =
    std::default_initializable<T> && std::copy_constructible<T> &&
    std::assignable_from<T&, const T&>;

template <SPSCQueueElement T, std::size_t Capacity>
  requires(Capacity >= 2) && ((Capacity & (Capacity - 1)) == 0)
class SPSCQueue final {
  static constexpr std::size_t kMask = Capacity - 1;

 public:
  explicit SPSCQueue() : buffer_(new T[Capacity]) {}

  ~SPSCQueue() { delete[] buffer_; }

  bool enqueue(const T& item) noexcept {
    auto head = producer_.head.load(std::memory_order_relaxed);
    auto tail = consumer_.tail.load(std::memory_order_acquire);
    if (head - tail == Capacity) {
      return false;
    }

    buffer_[head & kMask] = item;
    producer_.head.store(head + 1, std::memory_order_release);
    return true;
  }

  void enqueue_spin(const T& item) noexcept {
    auto head = producer_.head.load(std::memory_order_relaxed);
    for (;;) {
      auto tail = consumer_.tail.load(std::memory_order_acquire);
      if (head - tail != Capacity)
        break;
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386) || \
    defined(_M_IX86)
      _mm_pause();
#else
      std::this_thread::yield();
#endif
    }
    buffer_[head & kMask] = item;
    producer_.head.store(head + 1, std::memory_order_release);
  }

  bool dequeue(T& item) noexcept {
    auto tail = consumer_.tail.load(std::memory_order_relaxed);
    auto head = producer_.head.load(std::memory_order_acquire);
    if (tail == head) {
      return false;
    }
    item = buffer_[tail & kMask];
    consumer_.tail.store(tail + 1, std::memory_order_release);
    return true;
  }

  [[nodiscard]] bool empty() const noexcept {
    return producer_.head.load(std::memory_order_acquire) ==
           consumer_.tail.load(std::memory_order_relaxed);
  }

  [[nodiscard]] bool full() const noexcept {
    auto head = producer_.head.load(std::memory_order_relaxed);
    auto tail = consumer_.tail.load(std::memory_order_acquire);
    return (head - tail) == Capacity;
  }

  SPSCQueue(const SPSCQueue&) = delete;
  SPSCQueue& operator=(const SPSCQueue&) = delete;

 private:
  T* const buffer_;

  struct alignas(kCachelineSize) Producer {
    std::atomic<std::size_t> head{0};
    std::array<char, kCachelineSize - sizeof(std::atomic<std::size_t>)> pad{};
  } producer_;

  struct alignas(kCachelineSize) Consumer {
    std::atomic<std::size_t> tail{0};
    std::array<char, kCachelineSize - sizeof(std::atomic<std::size_t>)> pad{};
  } consumer_;
};
}  // namespace common
#endif  //SPSCQUEUE_H