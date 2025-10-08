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

#pragma once

namespace common {
template <typename T>
class MemoryPool {
 public:
  explicit MemoryPool(std::size_t num_elems)
      : store_(num_elems), free_(num_elems) {
    for (size_t idx = 0; idx < num_elems; ++idx)
      free_[idx] = num_elems - 1 - idx;
  }

  template <typename... Args>
    requires(std::is_nothrow_constructible_v<T, Args...>)
  T* allocate(Args&&... args) noexcept {
    if (free_.empty())
      return nullptr;
    std::size_t idx = free_.back();
    free_.pop_back();

    Bin& bin = store_[idx];
    T* pointer = std::construct_at(reinterpret_cast<T*>(bin.storage.data()),
                                   std::forward<Args>(args)...);
    bin.alive = true;
    return pointer;
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
    if (!bin.alive)
      return false;

    std::destroy_at(const_cast<T*>(elem));
    bin.alive = false;
    free_.push_back(idx);
    return true;
  }
  [[nodiscard]] std::size_t capacity() const noexcept { return store_.size(); }
  [[nodiscard]] std::size_t free_count() const noexcept { return free_.size(); }

  MemoryPool() = delete;

  MemoryPool(const MemoryPool&) = delete;

  MemoryPool(const MemoryPool&&) = delete;

  MemoryPool& operator=(const MemoryPool&) = delete;

  MemoryPool& operator=(const MemoryPool&&) = delete;

 private:
  struct Bin {
    alignas(T) std::array<std::byte, sizeof(T)> storage;
    bool alive = false;
  };

  std::vector<Bin> store_;
  std::vector<std::size_t> free_;
};
}  // namespace common