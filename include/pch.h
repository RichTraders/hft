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

#ifndef PCH_H
#define PCH_H

#include <algorithm>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <random>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "absl/container/flat_hash_map.h"

template <std::size_t N>
struct FixedString {
  char v[N];

  constexpr FixedString(const char (&s)[N]) { std::copy_n(s, N, v); }

  constexpr bool operator==(const FixedString&) const = default;

  // 편의 API들
  constexpr const char* c_str() const { return v; }  // N>=1 가정
  constexpr std::size_t size() const {
    return N > 0 ? N - 1 : 0;
  }  // 널 제외 길이
  std::string_view view() const { return std::string_view(v, size()); }
  std::string str() const { return std::string(v, size()); }
};

// ---- 자유 함수 연산자들 ----

// std::string + FixedString
template <std::size_t N>
std::string operator+(const std::string& lhs, const FixedString<N>& rhs) {
  std::string out;
  out.reserve(lhs.size() + rhs.size());
  out += lhs;
  out.append(rhs.c_str(), rhs.size());
  return out;
}

// FixedString + std::string
template <std::size_t N>
std::string operator+(const FixedString<N>& lhs, const std::string& rhs) {
  std::string out;
  out.reserve(lhs.size() + rhs.size());
  out.append(lhs.c_str(), lhs.size());
  out += rhs;
  return out;
}

// const char* + FixedString
template <std::size_t N>
std::string operator+(const char* lhs, const FixedString<N>& rhs) {
  std::string out(lhs);
  out.append(rhs.c_str(), rhs.size());
  return out;
}

// FixedString + const char*
template <std::size_t N>
std::string operator+(const FixedString<N>& lhs, const char* rhs) {
  std::string out(lhs.c_str(), lhs.size());
  out += rhs;
  return out;
}

// (선택) FixedString + FixedString
template <std::size_t A, std::size_t B>
std::string operator+(const FixedString<A>& lhs, const FixedString<B>& rhs) {
  std::string out;
  out.reserve(lhs.size() + rhs.size());
  out.append(lhs.c_str(), lhs.size());
  out.append(rhs.c_str(), rhs.size());
  return out;
}

// (선택) ostream 출력
template <std::size_t N>
std::ostream& operator<<(std::ostream& os, const FixedString<N>& s) {
  return os.write(s.c_str(), static_cast<std::streamsize>(s.size()));
}

#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

// 매크로 편의 함수
#define LOG_INFO(logger, text) \
  logger.log(LogLevel::kInfo, __FILE__, __LINE__, __func__, text)

#define LOG_DEBUG(logger, text) \
  logger.log(LogLevel::kDebug, __FILE__, __LINE__, __func__, text)

#define LOG_ERROR(logger, text) \
  logger.log(LogLevel::kError, __FILE__, __LINE__, __func__, text)

#endif  // PCH_H