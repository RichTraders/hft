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
constexpr int kDefaultChunkSize = 512;
template <typename T, size_t ChunkSize = kDefaultChunkSize>
class MPSCSegQueue {
  struct Chunk {
    std::atomic<size_t> idx{0};
    std::atomic<Chunk*> next{nullptr};
    T data[ChunkSize];  // NOLINT(modernize-avoid-c-arrays)
  };

  std::atomic<Chunk*> tail_;
  Chunk* head_;
  size_t head_pos_{0};

 public:
  MPSCSegQueue() {
    auto* dummy = new Chunk();
    head_ = dummy;
    tail_.store(dummy, std::memory_order_relaxed);
  }

  ~MPSCSegQueue() {
    T tmp;
    while (dequeue(tmp)) {}
    delete head_;
  }

  template <typename U>
  void enqueue(U&& input) {
    while (true) {
      Chunk* cur = tail_.load(std::memory_order_acquire);
      size_t pos = cur->idx.fetch_add(1, std::memory_order_acq_rel);

      if (pos < ChunkSize) {
        cur->data[pos] = std::forward<U>(input);
        return;
      }

      Chunk* next = cur->next.load(std::memory_order_acquire);
      if (!next) {
        auto* new_chuck = new Chunk();
        if (cur->next.compare_exchange_strong(next, new_chuck,
                                              std::memory_order_release,
                                              std::memory_order_relaxed)) {
          next = new_chuck;
        } else {
          delete new_chuck;
        }
      }

      tail_.compare_exchange_strong(cur, next, std::memory_order_release,
                                    std::memory_order_relaxed);
    }
  }

  bool dequeue(T& out) {
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

    return dequeue(out);
  }

  [[nodiscard]] bool empty() const {

    if (head_pos_ < head_->idx.load(std::memory_order_acquire))
      return false;

    return head_->next.load(std::memory_order_acquire) == nullptr;
  }
};
}  // namespace common