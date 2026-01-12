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

#ifndef SCOPE_EXIT_H_
#define SCOPE_EXIT_H_

#include <type_traits>
#include <utility>

template <class Functor>
class ScopeExit {
 public:
  explicit ScopeExit(Functor functor) noexcept : functor_(std::move(functor)) {}
  ScopeExit(const ScopeExit&) = delete;
  ScopeExit& operator=(const ScopeExit&) = delete;
  ScopeExit(ScopeExit&&) = delete;
  ScopeExit& operator=(ScopeExit&&) = delete;
  ~ScopeExit() {
    if (active_)
      functor_();
  }
  void release() noexcept { active_ = false; }

 private:
  Functor functor_;
  bool active_ = true;
};
template <class Functor>
auto MakeScopeExit(Functor&& functor) {
  return ScopeExit<std::decay_t<Functor>>(std::forward<Functor>(functor));
}

#endif  // SCOPE_EXIT_H_