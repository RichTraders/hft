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

#include <pch.h>

constexpr std::size_t kCachelineSize = 64;

template <typename T>
class SPSCQueue final {
 public:
  explicit SPSCQueue(std::size_t capacity)
      : capacity_(capacity + 1), buffer_(new T[capacity_]) {}

  ~SPSCQueue() { delete[] buffer_; }

  bool enqueue(const T& item) noexcept {
    const std::size_t next = (producer_.head + 1) % capacity_;
    if (next == consumer_.tail) {
      return false;
    }
    buffer_[producer_.head] = item;
    producer_.head = next;
    return true;
  }

  bool dequeue(T& item) noexcept {
    if (consumer_.tail == producer_.head) {
      return false;
    }
    item = buffer_[consumer_.tail];
    consumer_.tail = (consumer_.tail + 1) % capacity_;
    return true;
  }

  [[nodiscard]] bool empty() const noexcept {
    return producer_.head == consumer_.tail;
  }

  [[nodiscard]] bool full() const noexcept {
    return ((producer_.head + 1) % capacity_) == consumer_.tail;
  }

 private:
  const std::size_t capacity_;
  T* const buffer_;

  struct alignas(kCachelineSize) Producer {
    std::size_t head{0};
    std::array<char, kCachelineSize - sizeof(std::size_t)> pad;
  } producer_;

  struct alignas(kCachelineSize) Consumer {
    std::size_t tail{0};
    std::array<char, kCachelineSize - sizeof(std::size_t)> pad;
  } consumer_;
};

#endif  //SPSCQUEUE_H