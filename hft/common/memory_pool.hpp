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

#ifndef COMMON_MEMORY_POOL_H
#define COMMON_MEMORY_POOL_H

#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <type_traits>
#include <vector>

namespace common {
template <typename T>
class MemoryPool {
 public:
  explicit MemoryPool(std::size_t num_elems)
      : store_(num_elems), free_nodes_(num_elems) {
    for (std::size_t i = 0; i < num_elems; ++i) {
      free_nodes_[i].next = (i == num_elems - 1) ? kInvalidIndex : (i + 1);
    }

    const StackHead initial_head{0, 0};
    free_head_.store(pack(initial_head), std::memory_order_relaxed);
  }

  template <typename... Args>
    requires(std::is_nothrow_constructible_v<T, Args...>)
  T* allocate(Args&&... args) noexcept {
    // Lock-free pop from stack
    while (true) {
      std::uint64_t old_raw = free_head_.load(std::memory_order_acquire);
      const StackHead old_head = unpack(old_raw);

      if (old_head.index == kInvalidIndex) {
        return nullptr;
      }
      StackHead new_head;
      const std::uint32_t old_idx = old_head.index;
      const std::uint32_t next_idx = free_nodes_[old_idx].next;
      new_head.index = next_idx;
      new_head.counter = old_head.counter + 1;  // ABA guard

      const std::uint64_t new_raw = pack(new_head);

      if (free_head_.compare_exchange_weak(old_raw,
              new_raw,
              std::memory_order_acq_rel,
              std::memory_order_acquire)) {

        Bin& bin = store_[old_idx];

        T* pointer = std::construct_at(reinterpret_cast<T*>(bin.storage.data()),
            std::forward<Args>(args)...);
        bin.alive.store(true, std::memory_order::release);
        return pointer;
      }
    }
  }

  bool deallocate(const T* elem) noexcept {
    static_assert(std::is_nothrow_destructible_v<T>,
        "T must be nothrow-destructible");
    if (elem == nullptr)
      return false;

    const auto* const base =
        reinterpret_cast<const unsigned char*>(store_.data());
    const auto* const cur = reinterpret_cast<const unsigned char*>(elem);
    const std::ptrdiff_t off = cur - base;

    if (off < 0)
      return false;
    if (static_cast<std::size_t>(off) % sizeof(Bin) != 0)
      return false;

    const auto idx = off / sizeof(Bin);
    if (idx >= store_.size())
      return false;

    Bin& bin = store_[idx];

    // Atomic check-and-clear alive flag
    bool expected = true;
    if (!bin.alive.compare_exchange_strong(expected,
            false,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
      return false;
    }

    std::destroy_at(const_cast<T*>(elem));

    while (true) {
      StackHead new_head;
      std::uint64_t old_raw = free_head_.load(std::memory_order_acquire);
      const StackHead old_head = unpack(old_raw);

      free_nodes_[idx].next = old_head.index;

      new_head.index = idx;
      new_head.counter = old_head.counter + 1;  // Increment to prevent ABA

      const std::uint64_t new_raw = pack(new_head);

      if (free_head_.compare_exchange_weak(old_raw,
              new_raw,
              std::memory_order_acq_rel,
              std::memory_order_acquire)) {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] std::size_t capacity() const noexcept { return store_.size(); }

  // Count nodes in free list (not thread-safe, for debugging only)
  [[nodiscard]] std::size_t free_count() const noexcept {
    std::size_t count = 0;
    std::uint64_t raw = free_head_.load(std::memory_order_acquire);
    StackHead head = unpack(raw);
    std::size_t idx = head.index;

    while (idx != kInvalidIndex && count < store_.size()) {
      ++count;
      idx = free_nodes_[idx].next;
    }
    return count;
  }

  MemoryPool() = delete;
  MemoryPool(const MemoryPool&) = delete;
  MemoryPool(const MemoryPool&&) = delete;
  MemoryPool& operator=(const MemoryPool&) = delete;
  MemoryPool& operator=(const MemoryPool&&) = delete;

 private:
  static constexpr std::uint64_t kIndexMask = 0xFFFFFFFFULL;
  static constexpr uint32_t kCounterShift = 32UL;
  static constexpr uint32_t kAligned8Bytes = 8UL;
  static constexpr uint32_t kCacheLineSize = 64UL;
  static constexpr std::uint32_t kInvalidIndex =
      std::numeric_limits<std::uint32_t>::max();

  struct alignas(kAligned8Bytes) StackHead {
    std::uint32_t index;
    std::uint32_t counter;
    bool operator==(const StackHead& other) const {
      return index == other.index && counter == other.counter;
    }
  };

  static std::uint64_t pack(StackHead head) noexcept {
    return (static_cast<std::uint64_t>(head.counter) << kCounterShift) |
           static_cast<std::uint64_t>(head.index);
  }

  [[nodiscard]] StackHead unpack(std::uint64_t val) const noexcept {
    StackHead head;
    head.index = static_cast<std::uint32_t>(val & kIndexMask);
    head.counter = static_cast<std::uint32_t>(val >> kCounterShift);
    return head;
  }

  static_assert(sizeof(StackHead) == kAligned8Bytes,
      "StackHead must be 8 bytes");
  static_assert(alignof(StackHead) == kAligned8Bytes,
      "StackHead must be 8-byte aligned");

  struct Bin {
    alignas(T) std::array<std::byte, sizeof(T)> storage;
    std::atomic<bool> alive{false};
  };

  struct FreeNode {
    std::size_t next{kInvalidIndex};
  };

  std::vector<Bin> store_;
  std::vector<FreeNode> free_nodes_;
  alignas(kCacheLineSize) std::atomic<std::uint64_t> free_head_;
};

}  // namespace common

#endif