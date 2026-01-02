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

#ifndef FIXED_POINT_HPP
#define FIXED_POINT_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

#include "fixed_point_config.hpp"

namespace common {

constexpr int kDecimalBase = 10;
constexpr double kRoundingOffset = 0.5;
constexpr int64_t kScale8Decimal = 100000000;

// Scale=10^8 전용 역수 곱셈 상수
constexpr __int128_t kInverseScale8 =
    (static_cast<__int128_t>(1) << 64) / kScale8Decimal + 1;

template <int64_t Scale>
inline int64_t string_to_fixed(const char* str, size_t len) noexcept {
  int64_t integer_part = 0;
  int64_t frac_part = 0;
  int frac_digits = 0;
  bool in_frac = false;
  bool negative = false;

  for (size_t idx = 0; idx < len; ++idx) {
    const char chr = str[idx];
    if (chr == '-') {
      negative = true;
      continue;
    }
    if (chr == '.') {
      in_frac = true;
      continue;
    }
    if (chr >= '0' && chr <= '9') {
      if (in_frac) {
        frac_part = frac_part * kDecimalBase + (chr - '0');
        frac_digits++;
      } else {
        integer_part = integer_part * kDecimalBase + (chr - '0');
      }
    }
  }

  int64_t frac_scale = 1;
  for (int i = 0; i < frac_digits; ++i) {
    frac_scale *= kDecimalBase;
  }

  int64_t result = integer_part * Scale + (frac_part * Scale / frac_scale);
  return negative ? -result : result;
}

template <typename BaseType, BaseType Scale>
class FixedPoint {
 public:
  static constexpr BaseType kInvalidValue = std::numeric_limits<BaseType>::max();

  BaseType value;

  constexpr FixedPoint() noexcept : value(kInvalidValue) {}

  static constexpr FixedPoint from_raw(BaseType value) noexcept {
    FixedPoint result;
    result.value = value;
    return result;
  }

  // For test
  static constexpr FixedPoint from_double(double val) noexcept {
    return from_raw(static_cast<BaseType>(
        val * Scale + (val >= 0 ? kRoundingOffset : -kRoundingOffset)));
  }

  // Create from human-readable integer value (e.g., 100 qty -> 100 * Scale)
  static constexpr FixedPoint from_int64(int64_t val) noexcept {
    return from_raw(static_cast<BaseType>(val * Scale));
  }

  constexpr explicit FixedPoint(double val) noexcept
      : value(static_cast<BaseType>(
            val * Scale + (val >= 0 ? kRoundingOffset : -kRoundingOffset))) {}

  static FixedPoint from_string(const char* str, size_t len) noexcept {
    return from_raw(string_to_fixed<Scale>(str, len));
  }

  constexpr FixedPoint operator+(const FixedPoint& other) const noexcept {
    return from_raw(value + other.value);
  }

  constexpr FixedPoint operator-(const FixedPoint& other) const noexcept {
    return from_raw(value - other.value);
  }

  constexpr FixedPoint& operator+=(const FixedPoint& other) noexcept {
    value += other.value;
    return *this;
  }

  constexpr FixedPoint& operator-=(const FixedPoint& other) noexcept {
    value -= other.value;
    return *this;
  }

  constexpr FixedPoint operator*(const FixedPoint& other) const noexcept {
    using BigType = __int128_t;
    BigType temp =
        static_cast<BigType>(value) * static_cast<BigType>(other.value);

    if constexpr (Scale == kScale8Decimal) {
      constexpr int kShiftBits = 64;
      const BigType result = (temp * kInverseScale8) >> kShiftBits;
      return from_raw(static_cast<BaseType>(result));
    } else {
      return from_raw(static_cast<BaseType>(temp / Scale));
    }
  }

  constexpr FixedPoint operator/(const FixedPoint& other) const noexcept {
    using BigType = __int128_t;
    BigType temp = static_cast<BigType>(value) * Scale;
    return from_raw(static_cast<BaseType>(temp / other.value));
  }

  constexpr bool operator==(const FixedPoint& other) const noexcept {
    return value == other.value;
  }
  constexpr bool operator!=(const FixedPoint& other) const noexcept {
    return value != other.value;
  }
  constexpr bool operator<(const FixedPoint& other) const noexcept {
    return value < other.value;
  }
  constexpr bool operator>(const FixedPoint& other) const noexcept {
    return value > other.value;
  }
  constexpr bool operator<=(const FixedPoint& other) const noexcept {
    return value <= other.value;
  }
  constexpr bool operator>=(const FixedPoint& other) const noexcept {
    return value >= other.value;
  }

  constexpr bool operator==(double other) const noexcept {
    if (other == std::numeric_limits<double>::max()) {
      return !is_valid();
    }
    return to_double() == other;
  }
  constexpr bool operator!=(double other) const noexcept {
    return !(*this == other);
  }
  constexpr bool operator<(double other) const noexcept {
    return to_double() < other;
  }
  constexpr bool operator>(double other) const noexcept {
    return to_double() > other;
  }

  [[nodiscard]] constexpr bool is_valid() const noexcept {
    return value != kInvalidValue;
  }

  [[nodiscard]] double get_value() const noexcept {
    return to_double();
  }

  [[nodiscard]] double to_double() const noexcept {
    return static_cast<double>(value) / Scale;
  }

  [[nodiscard]] double to_double_truncated(int precision) const noexcept {
    // precision=4, Scale=10^8 → divisor = 10^4 = 10000
    constexpr int kMaxPrecision = 8;
    BaseType divisor = 1;
    for (int i = precision; i < kMaxPrecision; ++i) {
      divisor *= kDecimalBase;
    }
    BaseType truncated = (value / divisor) * divisor;
    return static_cast<double>(truncated) / Scale;
  }

  [[nodiscard]] double to_double_truncated_fast(
      BaseType divisor) const noexcept {
    BaseType truncated = (value / divisor) * divisor;
    return static_cast<double>(truncated) / Scale;
  }

  size_t to_string(char* buf, size_t buf_size) const noexcept {
    if (buf_size == 0)
      return 0;

    BaseType val = value;
    bool negative = val < 0;
    if (negative)
      val = -val;

    BaseType integer_part = val / Scale;
    BaseType frac_part = val % Scale;

    int frac_digits = 0;
    BaseType temp_scale = Scale;
    while (temp_scale > 1) {
      temp_scale /= kDecimalBase;
      ++frac_digits;
    }

    while (frac_digits > 0 && frac_part % kDecimalBase == 0) {
      frac_part /= kDecimalBase;
      --frac_digits;
    }

    constexpr int kBufferSize = 64;
    char temp[kBufferSize];  // NOLINT(modernize-avoid-c-arrays)
    int pos = 0;

    if (frac_digits > 0) {
      for (int i = 0; i < frac_digits; ++i) {
        temp[pos++] = '0' + (frac_part % kDecimalBase);
        frac_part /= kDecimalBase;
      }
      temp[pos++] = '.';
    }

    if (integer_part == 0) {
      temp[pos++] = '0';
    } else {
      while (integer_part > 0) {
        temp[pos++] = '0' + (integer_part % kDecimalBase);
        integer_part /= kDecimalBase;
      }
    }

    if (negative) {
      temp[pos++] = '-';
    }

    auto len = static_cast<size_t>(pos);
    if (len >= buf_size)
      len = buf_size - 1;

    for (size_t i = 0; i < len; ++i) {
      buf[i] = temp[pos - 1 - static_cast<int>(i)];
    }
    buf[len] = '\0';

    return len;
  }

  static constexpr BaseType scale() noexcept { return Scale; }

  size_t to_string2(char* buf, size_t buf_size) const noexcept {
    // NOLINTNEXTLINE(modernize-avoid-c-arrays)
    static constexpr char kDigitsLut[200] = {'0',
        '0',
        '0',
        '1',
        '0',
        '2',
        '0',
        '3',
        '0',
        '4',
        '0',
        '5',
        '0',
        '6',
        '0',
        '7',
        '0',
        '8',
        '0',
        '9',
        '1',
        '0',
        '1',
        '1',
        '1',
        '2',
        '1',
        '3',
        '1',
        '4',
        '1',
        '5',
        '1',
        '6',
        '1',
        '7',
        '1',
        '8',
        '1',
        '9',
        '2',
        '0',
        '2',
        '1',
        '2',
        '2',
        '2',
        '3',
        '2',
        '4',
        '2',
        '5',
        '2',
        '6',
        '2',
        '7',
        '2',
        '8',
        '2',
        '9',
        '3',
        '0',
        '3',
        '1',
        '3',
        '2',
        '3',
        '3',
        '3',
        '4',
        '3',
        '5',
        '3',
        '6',
        '3',
        '7',
        '3',
        '8',
        '3',
        '9',
        '4',
        '0',
        '4',
        '1',
        '4',
        '2',
        '4',
        '3',
        '4',
        '4',
        '4',
        '5',
        '4',
        '6',
        '4',
        '7',
        '4',
        '8',
        '4',
        '9',
        '5',
        '0',
        '5',
        '1',
        '5',
        '2',
        '5',
        '3',
        '5',
        '4',
        '5',
        '5',
        '5',
        '6',
        '5',
        '7',
        '5',
        '8',
        '5',
        '9',
        '6',
        '0',
        '6',
        '1',
        '6',
        '2',
        '6',
        '3',
        '6',
        '4',
        '6',
        '5',
        '6',
        '6',
        '6',
        '7',
        '6',
        '8',
        '6',
        '9',
        '7',
        '0',
        '7',
        '1',
        '7',
        '2',
        '7',
        '3',
        '7',
        '4',
        '7',
        '5',
        '7',
        '6',
        '7',
        '7',
        '7',
        '8',
        '7',
        '9',
        '8',
        '0',
        '8',
        '1',
        '8',
        '2',
        '8',
        '3',
        '8',
        '4',
        '8',
        '5',
        '8',
        '6',
        '8',
        '7',
        '8',
        '8',
        '8',
        '9',
        '9',
        '0',
        '9',
        '1',
        '9',
        '2',
        '9',
        '3',
        '9',
        '4',
        '9',
        '5',
        '9',
        '6',
        '9',
        '7',
        '9',
        '8',
        '9',
        '9'};

    if (buf_size == 0)
      return 0;

    BaseType val = value;
    bool negative = val < 0;
    if (negative)
      val = -val;

    BaseType integer_part = val / Scale;
    BaseType frac_part = val % Scale;

    int frac_digits = 0;
    BaseType temp_scale = Scale;
    while (temp_scale > 1) {
      temp_scale /= kDecimalBase;
      ++frac_digits;
    }

    while (frac_digits > 0 && frac_part % kDecimalBase == 0) {
      frac_part /= kDecimalBase;
      --frac_digits;
    }

    constexpr int kTempBufferSize = 64;
    constexpr unsigned kLutBase = 100;
    char temp[kTempBufferSize];  // NOLINT(modernize-avoid-c-arrays)
    int pos = 0;

    if (frac_digits > 0) {
      while (frac_digits >= 2) {
        unsigned idx = (frac_part % kLutBase) * 2;
        frac_part /= kLutBase;
        temp[pos++] = kDigitsLut[idx + 1];
        temp[pos++] = kDigitsLut[idx];
        frac_digits -= 2;
      }
      if (frac_digits == 1) {
        temp[pos++] = '0' + (frac_part % kDecimalBase);
      }
      temp[pos++] = '.';
    }

    if (integer_part == 0) {
      temp[pos++] = '0';
    } else {
      while (integer_part >= kLutBase) {
        unsigned idx = (integer_part % kLutBase) * 2;
        integer_part /= kLutBase;
        temp[pos++] = kDigitsLut[idx + 1];
        temp[pos++] = kDigitsLut[idx];
      }
      if (integer_part < kDecimalBase) {
        temp[pos++] = '0' + static_cast<int>(integer_part);
      } else {
        unsigned idx = static_cast<unsigned>(integer_part) * 2;
        temp[pos++] = kDigitsLut[idx + 1];
        temp[pos++] = kDigitsLut[idx];
      }
    }

    if (negative) {
      temp[pos++] = '-';
    }

    auto len = static_cast<size_t>(pos);
    if (len >= buf_size)
      len = buf_size - 1;

    for (size_t i = 0; i < len; ++i) {
      buf[i] = temp[pos - 1 - static_cast<int>(i)];
    }
    buf[len] = '\0';

    return len;
  }
};

using FixedPrice = FixedPoint<int64_t, FixedPointConfig::kPriceScale>;
using FixedQty = FixedPoint<int64_t, FixedPointConfig::kQtyScale>;

using FixedPrice8 = FixedPoint<int64_t, kScale8Decimal>;
using FixedQty8 = FixedPoint<int64_t, kScale8Decimal>;

inline auto toString(FixedPrice price) -> std::string {
  return std::to_string(price.to_double());
}

inline auto toString(FixedQty qty) -> std::string {
  return std::to_string(qty.to_double());
}

// kQtyScale (internal) → kQtyScaleActual (exchange)
inline double qty_to_actual_double(FixedQty qty) noexcept {
  constexpr int64_t kScaleRatio = FixedPointConfig::kQtyScale / FixedPointConfig::kQtyScaleActual;
  const int64_t truncated = (qty.value / kScaleRatio) * kScaleRatio;
  return static_cast<double>(truncated) / FixedPointConfig::kQtyScale;
}

// kPriceScale (internal) → kPriceScaleActual (exchange)
inline double price_to_actual_double(FixedPrice price) noexcept {
  constexpr int64_t kScaleRatio = FixedPointConfig::kPriceScale / FixedPointConfig::kPriceScaleActual;
  const int64_t truncated = (price.value / kScaleRatio) * kScaleRatio;
  return static_cast<double>(truncated) / FixedPointConfig::kPriceScale;
}

}  // namespace common

#endif  // FIXED_POINT_HPP
