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

  std::atomic<Chunk*> _tail;
  Chunk*              _head;
  size_t              _head_pos{0};

public:
  MPSCSegQueue() {
    auto* dummy = new Chunk();
    _head = dummy;
    _tail.store(dummy, std::memory_order_relaxed);
  }

  ~MPSCSegQueue() {
    T tmp;
    while (dequeue(tmp));
    delete _head;
  }

  template<typename U>
  void enqueue(U&& v) {
    while (true) {
      Chunk* cur = _tail.load(std::memory_order_acquire);
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

      _tail.compare_exchange_strong(
          cur, next,
          std::memory_order_release,
          std::memory_order_relaxed);
    }
  }

  bool dequeue(T& out) {
    if (_head_pos < ChunkSize) {
      if (_head_pos >= _head->idx.load(std::memory_order_acquire))
        return false;
      out = std::move(_head->data[_head_pos++]);
      return true;
    }

    Chunk* nxt = _head->next.load(std::memory_order_acquire);
    if (!nxt)
      return false;

    delete _head;
    _head = nxt;
    _head_pos = 0;

    return dequeue(out);
  }

  bool empty() const {

    if (_head_pos < _head->idx.load(std::memory_order_acquire))
      return false;

    return _head->next.load(std::memory_order_acquire) == nullptr;
  }
};
}