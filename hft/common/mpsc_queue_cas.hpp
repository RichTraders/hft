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

#pragma once

namespace common {
template<typename T, size_t ChunkSize = 64>
class MPSCSegQueue {
private:
  struct Chunk {
    std::atomic<size_t> idx{0};
    std::atomic<Chunk*> next{nullptr};
    T                   data[ChunkSize];
  };

  std::atomic<Chunk*> tail_;
  Chunk*              head_;
  size_t              head_pos_{0};

public:
  MPSCSegQueue() {
    auto* dummy = new Chunk();
    head_ = dummy;
    tail_.store(dummy, std::memory_order_relaxed);
  }

  ~MPSCSegQueue() {
    T tmp;
    while (pop(tmp));
    delete head_;
  }

  template<typename U>
  void push(U&& v) {
    while (true) {
      Chunk* cur = tail_.load(std::memory_order_acquire);
      size_t pos = cur->idx.fetch_add(1, std::memory_order_acq_rel);

      if (pos < ChunkSize) {
        cur->data[pos] = std::forward<U>(v);
        return;
      }

      Chunk* next = cur->next.load(std::memory_order_acquire);
      if (!next) {
        Chunk* _new = new Chunk();
        if (cur->next.compare_exchange_strong(
                next, _new,
                std::memory_order_release,
                std::memory_order_relaxed)) {
          next = _new;
                } else {
                  delete _new;
                }
      }

      tail_.compare_exchange_strong(
          cur, next,
          std::memory_order_release,
          std::memory_order_relaxed);
    }
  }

  bool pop(T& out) {
    if (head_pos_ < ChunkSize) {
      if (head_pos_ >= head_->idx.load(std::memory_order_acquire))
        return false;
      out = std::move(head_->data[head_pos_++]);
      return true;
    }

    Chunk* nxt = head_->next.load(std::memory_order_acquire);
    if (!nxt)
      return false;

    delete head_;
    head_ = nxt;
    head_pos_ = 0;

    return pop(out);
  }

  bool empty() const {

    if (head_pos_ < head_->idx.load(std::memory_order_acquire))
      return false;

    return head_->next.load(std::memory_order_acquire) == nullptr;
  }
};
}