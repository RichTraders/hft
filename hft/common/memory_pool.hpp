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
  explicit MemoryPool(int num_elems) : store_(num_elems, {T(), true}) {
    if (reinterpret_cast<const ObjectBin*>(&(store_[0].object_)) ==
        store_.data()) {
      next_free_index_ = 0;
    } else
      next_free_index_ = -1;
  }

  template <typename... Args>
  T* allocate(Args... args) noexcept {
    if (next_free_index_ == -1)
      return nullptr;

    auto obj_block = &(store_[next_free_index_]);

    if (!static_cast<bool>(obj_block->is_free_))
      return nullptr;

    T* ret = &(obj_block->object_);
    ret = new (ret) T(args...);  // placement new.
    obj_block->is_free_ = false;

    if (!static_cast<bool>(updateNextFreeIndex()))
      return nullptr;

    return ret;
  }

  bool deallocate(const T* elem) noexcept {
    if (elem == nullptr)
      return false;

    const auto elem_index =
        (reinterpret_cast<const ObjectBin*>(elem) - store_.data());
    if (elem_index < 0)
      return false;

    if (static_cast<size_t>(elem_index) >= store_.size())
      return false;

    if (static_cast<bool>(store_[elem_index].is_free_))
      return false;

    store_[elem_index].is_free_ = true;
    return true;
  }

  MemoryPool() = delete;

  MemoryPool(const MemoryPool&) = delete;

  MemoryPool(const MemoryPool&&) = delete;

  MemoryPool& operator=(const MemoryPool&) = delete;

  MemoryPool& operator=(const MemoryPool&&) = delete;

 private:
  bool updateNextFreeIndex() noexcept {
    const int initial_free_index = next_free_index_;
    while (!store_[next_free_index_].is_free_) {
      ++next_free_index_;
      if (UNLIKELY(next_free_index_ == static_cast<int>(store_.size()))) {
        // hardware branch predictor should almost always predict this to be false any ways.
        next_free_index_ = 0;
      }
      if (UNLIKELY(initial_free_index == next_free_index_)) {
        return false;
      }
    }

    return true;
  }

  struct ObjectBin {
    T object_;
    bool is_free_ = true;
  };

  std::vector<ObjectBin> store_;
  int next_free_index_ = -1;
};
}  // namespace common