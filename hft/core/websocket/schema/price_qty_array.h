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

#ifndef PRICE_QTY_ARRAY_H
#define PRICE_QTY_ARRAY_H

#include <array>
#include <charconv>
#include <cstddef>
#include <vector>

#include <glaze/glaze.hpp>
#include "common/fixed_point_config.hpp"

namespace schema {

// Helper to parse quoted decimal string to scaled int64_t
// e.g., "90558.30" with scale=10 -> 905583
template <int64_t Scale>
inline int64_t parse_quoted_decimal_to_int(const char* start,
    const char* end) noexcept {
  int64_t mantissa = 0;
  int frac_digits = 0;
  bool in_frac = false;

  constexpr int kDecimalBase = 10;
  for (const char* ptr = start; ptr < end; ++ptr) {
    const char chr = *ptr;
    if (chr >= '0' && chr <= '9') {
      mantissa = mantissa * kDecimalBase + (chr - '0');
      if (in_frac)
        ++frac_digits;
    } else if (chr == '.') {
      in_frac = true;
    }
  }

  // NOLINTBEGIN(modernize-avoid-c-arrays)
  static constexpr int64_t kPowersOf10[] = {1LL,
      10LL,
      100LL,
      1000LL,
      10000LL,
      100000LL,
      1000000LL,
      10000000LL,
      100000000LL,
      1000000000LL,
      10000000000LL};
  // NOLINTEND(modernize-avoid-c-arrays)

  if (frac_digits == 0) {
    return mantissa * Scale;
  }

  const int64_t scale_divisor = kPowersOf10[frac_digits];
  // If Scale >= scale_divisor: multiply (e.g., "1.5" with Scale=1000 -> 1500)
  // If Scale < scale_divisor: divide (e.g., "1000.00" with Scale=10 -> 10000)
  if (Scale >= scale_divisor) {
    return mantissa * (Scale / scale_divisor);
  }
  return mantissa / (scale_divisor / Scale);
}

struct PriceQtyArray {
  std::vector<std::array<double, 2>> data;
  PriceQtyArray() = default;
  explicit PriceQtyArray(std::vector<std::array<double, 2>>&& arr)
      : data(std::move(arr)) {}
  [[nodiscard]] auto begin() const { return data.begin(); }
  [[nodiscard]] auto end() const { return data.end(); }
  [[nodiscard]] size_t size() const { return data.size(); }
  [[nodiscard]] bool empty() const { return data.empty(); }
  const std::array<double, 2>& operator[](size_t idx) const { return data[idx]; }
};

}  // namespace schema

template <>
struct glz::meta<::schema::PriceQtyArray> {
  // NOLINTBEGIN(readability-identifier-naming) - glaze library requires these names
  static constexpr auto custom_read = true;
  static constexpr auto custom_write = true;
  // NOLINTEND(readability-identifier-naming)
};
template <>
struct glz::detail::from<glz::JSON, ::schema::PriceQtyArray> {
  template <glz::opts Opts, typename It, typename End>
  static void op(::schema::PriceQtyArray& value, glz::is_context auto&& /*ctx*/,
      It&& it,  // NOLINT(readability-identifier-length)
      End&& end) noexcept {
    value.data.clear();

    while (it != end &&
           (*it == ' ' || *it == '\t' || *it == '\n' || *it == '\r')) {
      ++it;
    }
    if (it == end || *it != '[') {
      return;
    }
    ++it;

    constexpr size_t kDefaultReserveSize = 1024;
    value.data.reserve(kDefaultReserveSize);
    while (it != end) {

      while (it != end &&
             (*it == ' ' || *it == '\t' || *it == '\n' || *it == '\r')) {
        ++it;
      }
      if (it == end)
        break;
      if (*it == ']') {
        ++it;
        break;
      }
      if (*it == ',') {
        ++it;
        continue;
      }

      if (*it != '[') {
        break;
      }
      ++it;
      std::array<double, 2> entry{};

      while (it != end && *it != '"')
        ++it;
      if (it == end)
        break;
      ++it;
      const char* price_start = &(*it);
      while (it != end && *it != '"')
        ++it;
      if (it == end)
        break;
      const char* price_end = &(*it);
      std::from_chars(price_start, price_end, entry[0]);
      ++it;

      while (it != end && *it != ',')
        ++it;
      if (it != end)
        ++it;

      while (it != end && *it != '"')
        ++it;
      if (it == end)
        break;
      ++it;
      const char* qty_start = &(*it);
      while (it != end && *it != '"')
        ++it;
      if (it == end)
        break;
      const char* qty_end = &(*it);
      std::from_chars(qty_start, qty_end, entry[1]);
      ++it;

      while (it != end && *it != ']')
        ++it;
      if (it != end)
        ++it;
      value.data.push_back(entry);
    }
  }
};

template <>
struct glz::detail::to<glz::JSON, ::schema::PriceQtyArray> {
  template <glz::opts Opts>
  static void op(const ::schema::PriceQtyArray& value,
      glz::is_context auto&& /*ctx*/, auto&&... args) noexcept {

    glz::detail::dump<'['>(args...);
    bool first = true;
    for (const auto& entry : value.data) {
      if (!first) {
        glz::detail::dump<','>(args...);
      }
      first = false;
      glz::detail::dump<'['>(args...);
      glz::detail::dump<'"'>(args...);
      glz::detail::write_chars::op<Opts>(entry[0], args...);
      glz::detail::dump<'"'>(args...);
      glz::detail::dump<','>(args...);
      glz::detail::dump<'"'>(args...);
      glz::detail::write_chars::op<Opts>(entry[1], args...);
      glz::detail::dump<'"'>(args...);
      glz::detail::dump<']'>(args...);
    }
    glz::detail::dump<']'>(args...);
  }
};

namespace schema {

template <int64_t PriceScale, int64_t QtyScale>
struct ScaledInt64PriceQtyArray {
  std::vector<std::array<int64_t, 2>> data;
  ScaledInt64PriceQtyArray() = default;
  explicit ScaledInt64PriceQtyArray(std::vector<std::array<int64_t, 2>>&& data)
      : data(std::move(data)) {}
  [[nodiscard]] auto begin() const { return data.begin(); }
  [[nodiscard]] auto end() const { return data.end(); }
  [[nodiscard]] size_t size() const { return data.size(); }
  [[nodiscard]] bool empty() const { return data.empty(); }
  const std::array<int64_t, 2>& operator[](size_t idx) const { return data[idx]; }
  void reserve(size_t count) { data.reserve(count); }
  void push_back(const std::array<int64_t, 2>& entry) { data.push_back(entry); }
};

using FixedPriceQtyArray =
    ScaledInt64PriceQtyArray<::common::FixedPointConfig::kPriceScale,
        ::common::FixedPointConfig::kQtyScale>;

}  // namespace schema

template <int64_t PriceScale, int64_t QtyScale>
struct glz::meta<::schema::ScaledInt64PriceQtyArray<PriceScale, QtyScale>> {
  // NOLINTBEGIN(readability-identifier-naming) - glaze library requires these names
  static constexpr auto custom_read = true;
  static constexpr auto custom_write = true;
  // NOLINTEND(readability-identifier-naming)
};

template <int64_t PriceScale, int64_t QtyScale>
struct glz::detail::from<glz::JSON,
    ::schema::ScaledInt64PriceQtyArray<PriceScale, QtyScale>> {
  template <glz::opts Opts, typename It, typename End>
  static void op(
      ::schema::ScaledInt64PriceQtyArray<PriceScale, QtyScale>& value,
      glz::is_context auto&& /*ctx*/, It&& iter, End&& end) noexcept {
    value.data.clear();

    while (iter != end &&
           (*iter == ' ' || *iter == '\t' || *iter == '\n' || *iter == '\r')) {
      ++iter;
    }
    if (iter == end || *iter != '[') {
      return;
    }
    ++iter;

    constexpr auto kDataSize = 1024;
    value.data.reserve(kDataSize);
    while (iter != end) {
      while (iter != end && (*iter == ' ' || *iter == '\t' || *iter == '\n' ||
                                *iter == '\r')) {
        ++iter;
      }
      if (iter == end)
        break;
      if (*iter == ']') {
        ++iter;
        break;
      }
      if (*iter == ',') {
        ++iter;
        continue;
      }

      if (*iter != '[')
        break;
      ++iter;

      std::array<int64_t, 2> entry{};

      while (iter != end && *iter != '"')
        ++iter;
      if (iter == end)
        break;
      ++iter;
      const char* price_start = &(*iter);
      while (iter != end && *iter != '"')
        ++iter;
      if (iter == end)
        break;
      const char* price_end = &(*iter);
      entry[0] = ::schema::parse_quoted_decimal_to_int<PriceScale>(price_start,
          price_end);
      ++iter;

      while (iter != end && *iter != ',')
        ++iter;
      if (iter != end)
        ++iter;

      while (iter != end && *iter != '"')
        ++iter;
      if (iter == end)
        break;
      ++iter;
      const char* qty_start = &(*iter);
      while (iter != end && *iter != '"')
        ++iter;
      if (iter == end)
        break;
      const char* qty_end = &(*iter);
      entry[1] =
          ::schema::parse_quoted_decimal_to_int<QtyScale>(qty_start, qty_end);
      ++iter;

      while (iter != end && *iter != ']')
        ++iter;
      if (iter != end)
        ++iter;
      value.data.push_back(entry);
    }
  }
};

template <int64_t PriceScale, int64_t QtyScale>
struct glz::detail::to<glz::JSON,
    ::schema::ScaledInt64PriceQtyArray<PriceScale, QtyScale>> {
  template <glz::opts Opts>
  static void op(
      const ::schema::ScaledInt64PriceQtyArray<PriceScale, QtyScale>& value,
      glz::is_context auto&& /*ctx*/, auto&&... args) noexcept {
    glz::detail::dump<'['>(args...);
    bool first = true;
    for (const auto& entry : value.data) {
      if (!first) {
        glz::detail::dump<','>(args...);
      }
      first = false;
      glz::detail::dump<'['>(args...);
      glz::detail::dump<'"'>(args...);
      glz::detail::write_chars::op<Opts>(entry[0], args...);
      glz::detail::dump<'"'>(args...);
      glz::detail::dump<','>(args...);
      glz::detail::dump<'"'>(args...);
      glz::detail::write_chars::op<Opts>(entry[1], args...);
      glz::detail::dump<'"'>(args...);
      glz::detail::dump<']'>(args...);
    }
    glz::detail::dump<']'>(args...);
  }
};

template <int64_t Scale>
struct ScaledInt64 {
  int64_t value{0};

  ScaledInt64() = default;
  // NOLINTNEXTLINE(google-explicit-constructor) - intentional implicit conversion
  ScaledInt64(int64_t val)
      : value(val) {}
  // NOLINTNEXTLINE(google-explicit-constructor) - intentional implicit conversion
  operator int64_t() const {
    return value;
  }
  ScaledInt64& operator=(int64_t val) {
    value = val;
    return *this;
  }
};

template <int64_t Scale>
struct glz::meta<::ScaledInt64<Scale>> {
  // NOLINTBEGIN(readability-identifier-naming) - glaze library requires these names
  static constexpr auto custom_read = true;
  static constexpr auto custom_write = true;
  // NOLINTEND(readability-identifier-naming)
};

template <int64_t Scale>
struct glz::detail::from<glz::JSON, ::ScaledInt64<Scale>> {
  template <glz::opts Opts, typename It, typename End>
  static void op(::ScaledInt64<Scale>& val, glz::is_context auto&& /*ctx*/,
      It&& it,  // NOLINT(readability-identifier-length)
      End&& end) noexcept {
    while (it != end &&
           (*it == ' ' || *it == '\t' || *it == '\n' || *it == '\r')) {
      ++it;
    }
    if (it == end || *it != '"') {
      return;
    }
    ++it;

    const char* start = &(*it);
    while (it != end && *it != '"')
      ++it;
    const char* str_end = &(*it);

    val.value = ::schema::parse_quoted_decimal_to_int<Scale>(start, str_end);

    if (it != end)
      ++it;
  }
};

template <int64_t Scale>
struct glz::detail::to<glz::JSON, ::ScaledInt64<Scale>> {
  template <glz::opts Opts>
  static void op(const ::ScaledInt64<Scale>& val,
      glz::is_context auto&& /*ctx*/, auto&&... args) noexcept {
    glz::detail::dump<'"'>(args...);
    glz::detail::write_chars::op<Opts>(val.value, args...);
    glz::detail::dump<'"'>(args...);
  }
};

#endif  // PRICE_QTY_ARRAY_H
