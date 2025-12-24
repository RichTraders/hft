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

#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <type_traits>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386) || \
    defined(_M_IX86)
#include <emmintrin.h>
#endif

namespace common {

constexpr int kDefaultChunkSize = 512;
constexpr std::size_t kCacheLine = 64;
constexpr int kMaxSpinCount = 32;

inline void cpu_relax() noexcept {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386) || \
    defined(_M_IX86)
  _mm_pause();
#else
  std::this_thread::yield();
#endif
}

template <typename T, size_t ChunkSize = kDefaultChunkSize,
    size_t CacheLine = kCacheLine>
class MPSCSegQueue {
  struct Slot {
    std::atomic<uint8_t> ready{0};
    alignas(T) std::array<unsigned char, sizeof(T)> storage;
    T* ptr() noexcept { return reinterpret_cast<T*>(storage.data()); }
    const T* ptr() const noexcept {
      return reinterpret_cast<const T*>(storage.data());
    }
  };
  struct alignas(CacheLine) Chunk {
    alignas(CacheLine) std::atomic<std::size_t> idx{0};
    alignas(CacheLine) std::atomic<Chunk*> next{nullptr};
    alignas(CacheLine) std::atomic<uint32_t> refs{0};
    alignas(CacheLine) std::array<Slot, ChunkSize> slots;
  };

  alignas(CacheLine) std::atomic<Chunk*> tail_;
  alignas(CacheLine) Chunk* head_;
  std::size_t head_pos_{0};

  struct RetiredNode {
    Chunk* chunk;
    RetiredNode* next;
  };

  std::atomic<RetiredNode*> retired_head_{nullptr};
  size_t retired_count_{0};
  static constexpr size_t kScanThreshold = 64;

  void retire_chunk(Chunk* chunk) noexcept {
    chunk->refs.fetch_sub(1, std::memory_order_acq_rel);

    auto* node = new RetiredNode{chunk, nullptr};
    RetiredNode* head = retired_head_.load(std::memory_order_relaxed);
    do {
      node->next = head;
    } while (!retired_head_.compare_exchange_weak(head,
        node,
        std::memory_order_release,
        std::memory_order_relaxed));
    ++retired_count_;
  }

  void try_reclaim() {
    if (retired_count_ < kScanThreshold)
      return;

    RetiredNode* list =
        retired_head_.exchange(nullptr, std::memory_order_acquire);
    retired_count_ = 0;

    RetiredNode* keep = nullptr;
    while (list) {
      RetiredNode* node = list;
      list = list->next;
      if (node->chunk->refs.load(std::memory_order_acquire) == 0) {
        delete node->chunk;
        delete node;
      } else {
        node->next = keep;
        keep = node;
        ++retired_count_;
      }
    }

    if (keep) {
      RetiredNode* head = retired_head_.load(std::memory_order_relaxed);
      do {
        RetiredNode* tail = keep;
        while (tail->next)
          tail = tail->next;
        tail->next = head;
      } while (!retired_head_.compare_exchange_weak(head,
          keep,
          std::memory_order_release,
          std::memory_order_relaxed));
    }
  }

 public:
  MPSCSegQueue() {
    auto* dummy = new Chunk();
    head_ = dummy;
    dummy->refs.store(2, std::memory_order_relaxed);
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
        Slot& slot = chunk->slots[idx];
        if (slot.ready.load(std::memory_order_acquire)) {
          std::destroy_at(slot.ptr());
          slot.ready.store(0, std::memory_order_relaxed);
        }
      }

      Chunk* next = chunk->next.load(std::memory_order_acquire);
      retire_chunk(chunk);

      chunk = next;
      first = false;
    }

    // 남은 것 정리: 여기서만 refs==0 인 것 delete
    try_reclaim();
    RetiredNode* rest =
        retired_head_.exchange(nullptr, std::memory_order_acquire);
    while (rest) {
      RetiredNode* node = rest;
      rest = rest->next;
      if (node->chunk->refs.load(std::memory_order_acquire) == 0)
        delete node->chunk;
      delete node;
    }
  }

  MPSCSegQueue(const MPSCSegQueue&) = delete;
  MPSCSegQueue& operator=(const MPSCSegQueue&) = delete;
  MPSCSegQueue(MPSCSegQueue&&) = delete;
  MPSCSegQueue& operator=(MPSCSegQueue&&) = delete;

  template <typename U>
    requires(std::constructible_from<T, U &&> &&
             std::is_nothrow_move_constructible_v<T> &&
             (std::is_nothrow_move_assignable_v<T> ||
                 std::is_trivially_copyable_v<T>))
  void enqueue(U&& input) noexcept(noexcept(T(std::forward<U>(input)))) {
    unsigned spin = 0;
    while (true) {
      Chunk* cur = tail_.load(std::memory_order_acquire);

      // producers' temporary ref
      cur->refs.fetch_add(1, std::memory_order_acq_rel);
      if (cur != tail_.load(std::memory_order_acquire)) {
        cur->refs.fetch_sub(1, std::memory_order_acq_rel);
        cpu_relax();
        continue;
      }

      size_t pos = cur->idx.fetch_add(1, std::memory_order_acq_rel);
      if (pos < ChunkSize) {
        Slot& slot = cur->slots[pos];
        std::construct_at(slot.ptr(), std::forward<U>(input));
        slot.ready.store(true, std::memory_order_release);
        cur->refs.fetch_sub(1, std::memory_order_acq_rel);
        return;
      }

      Chunk* next = cur->next.load(std::memory_order_acquire);
      if (!next) {
        auto* new_chunk = new Chunk();
        if (!cur->next.compare_exchange_strong(next,
                new_chunk,
                std::memory_order_acq_rel,
                std::memory_order_relaxed)) {
          delete new_chunk;
          cpu_relax();
        } else {
          next = new_chunk;
        }
      }

      if (!tail_.compare_exchange_strong(cur,
              next,
              std::memory_order_acq_rel,
              std::memory_order_relaxed)) {
        cpu_relax();
        if (++spin > kMaxSpinCount) {
          spin = 0;
          std::this_thread::yield();
        }
      } else {
        next->refs.fetch_add(1, std::memory_order_acq_rel);
        cur->refs.fetch_sub(1, std::memory_order_acq_rel);
      }

      cur->refs.fetch_sub(1, std::memory_order_acq_rel);
    }
  }

  bool dequeue(T& out) noexcept(std::is_nothrow_move_assignable_v<T>) {
    while (true) {
      if (head_pos_ < ChunkSize) {
        Slot& slot = head_->slots[head_pos_];
        if (!slot.ready.load(std::memory_order_acquire))
          return false;
        out = std::move(*slot.ptr());
        std::destroy_at(slot.ptr());
        slot.ready.store(0, std::memory_order_relaxed);
        ++head_pos_;

        if (head_pos_ == ChunkSize) {
          Chunk* old = head_;
          Chunk* next = old->next.load(std::memory_order_acquire);
          if (next) {
            next->refs.fetch_add(1, std::memory_order_acq_rel);
            head_ = next;
            head_pos_ = 0;
            retire_chunk(old);
            try_reclaim();
          }
        }
        return true;
      }

      Chunk* next = head_->next.load(std::memory_order_acquire);
      if (!next)
        return false;

      next->refs.fetch_add(1, std::memory_order_acq_rel);
      Chunk* old = head_;
      head_ = next;
      head_pos_ = 0;
      retire_chunk(old);
      try_reclaim();
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