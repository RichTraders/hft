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
constexpr std::size_t kCacheLine = 64;

template <typename T, size_t ChunkSize = kDefaultChunkSize,
          size_t CacheLine = kCacheLine>
class MPSCSegQueue {
  struct Slot {
    std::atomic<uint8_t> ready{0};
    alignas(T) std::array<unsigned char, sizeof(T)> storage;
    T* ptr() { return std::launder(reinterpret_cast<T*>(storage.data())); }
    const T* ptr() const {
      return std::launder(reinterpret_cast<const T*>(storage.data()));
    }
  };
  struct alignas(CacheLine) Chunk {
    alignas(CacheLine) std::atomic<std::size_t> idx{0};
    alignas(CacheLine) std::atomic<Chunk*> next{nullptr};
    alignas(CacheLine) std::array<Slot, ChunkSize> slots;
  };

  alignas(CacheLine) std::atomic<Chunk*> tail_;
  alignas(CacheLine) Chunk* head_;
  std::size_t head_pos_{0};

 public:
  MPSCSegQueue() {
    auto* dummy = new Chunk();
    head_ = dummy;
    tail_.store(dummy, std::memory_order_relaxed);
  }

  ~MPSCSegQueue() {
    T tmp;
    while (dequeue(tmp)) {}
    Chunk* chunk = head_;
    bool first = true;
    const size_t start_pos = head_pos_;

    while (chunk) {
      const size_t begin = first ? start_pos : 0;
      const size_t limit =
          std::min(chunk->idx.load(std::memory_order_acquire), ChunkSize);
      for (size_t idx = begin; idx < limit; ++idx) {
        const Slot& slot = chunk->slots[idx];
        if (slot.ready.load(std::memory_order_acquire)) {
          slot.ptr()->~T();
        }
      }
      Chunk* next = chunk->next.load(std::memory_order_acquire);
      delete chunk;
      chunk = next;
      first = false;
    }
  }

  MPSCSegQueue(const MPSCSegQueue&) = delete;
  MPSCSegQueue& operator=(const MPSCSegQueue&) = delete;
  MPSCSegQueue(MPSCSegQueue&&) = delete;
  MPSCSegQueue& operator=(MPSCSegQueue&&) = delete;

  template <typename U>
    requires(std::constructible_from<T, U &&>)
  void enqueue(U&& input) noexcept(noexcept(T(std::forward<U>(input)))) {
    while (true) {
      Chunk* cur = tail_.load(std::memory_order_acquire);
      size_t pos = cur->idx.fetch_add(1, std::memory_order_acq_rel);

      if (pos < ChunkSize) {
        Slot& slot = cur->slots[pos];
        new (slot.ptr()) T(std::forward<U>(input));
        slot.ready.store(true, std::memory_order_release);
        return;
      }

      Chunk* next = cur->next.load(std::memory_order_acquire);
      if (!next) {
        auto* new_chunk = new Chunk();
        if (cur->next.compare_exchange_strong(next, new_chunk,
                                              std::memory_order_acq_rel,
                                              std::memory_order_relaxed)) {
          next = new_chunk;
        } else {
          delete new_chunk;
        }
      }

      tail_.compare_exchange_strong(cur, next, std::memory_order_acq_rel,
                                    std::memory_order_relaxed);
    }
  }

  bool dequeue(T& out) {
    while (true) {
      if (head_pos_ < ChunkSize) {
        Slot& slot = head_->slots[head_pos_];
        if (!slot.ready.load(std::memory_order_acquire))
          return false;
        out = std::move(*slot.ptr());
        slot.ptr()->~T();
        ++head_pos_;

        if (head_pos_ == ChunkSize) {
          Chunk* old = head_;
          Chunk* nxt = old->next.load(std::memory_order_acquire);
          if (nxt) {
            head_ = nxt;
            head_pos_ = 0;
            delete old;  // 즉시 회수
          }
        }
        return true;
      }
      Chunk* nxt = head_->next.load(std::memory_order_acquire);
      if (!nxt)
        return false;
      Chunk* old = head_;
      head_ = nxt;
      head_pos_ = 0;
      delete old;
    }
  }

  [[nodiscard]] bool empty() const {
    if (head_pos_ < ChunkSize) {
      return !head_->slots[head_pos_].ready.load(std::memory_order_acquire);
    }
    return head_->next.load(std::memory_order_acquire) == nullptr;
  }
};
}  // namespace common