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

#ifndef FIXED_POINT_CONFIG_HPP
#define FIXED_POINT_CONFIG_HPP

#include <cstdint>

namespace common {

template <typename T>
concept IFixedPointConfig = requires {
    // 1. static constexpr int64_t 멤버들이 존재하고, 타입이 일치해야 함
    { T::kPriceScale } -> std::convertible_to<int64_t>;
    { T::kQtyScale }   -> std::convertible_to<int64_t>;
    { T::kPnlScale }   -> std::convertible_to<int64_t>;
    
    // 2. static constexpr int 멤버들이 존재해야 함
    { T::kPricePrecision } -> std::convertible_to<int>;
    { T::kQtyPrecision }   -> std::convertible_to<int>;

    // (선택) 값에 대한 컴파일 타임 검증도 가능 (예: Scale은 0보다 커야 한다)
    requires T::kPriceScale > 0;
};

// symbol (Select in CMake: -DFIXED_POINT_SYMBOL=BTCUSDC)

struct XRPUSDCConfig {
  static constexpr int64_t kPriceScale = 1'000'000;   // price precision=6 (internal)
  static constexpr int64_t kQtyScale = 1'000;        // qty precision=3 (internal)
  static constexpr int64_t kPnlScale = kPriceScale * kQtyScale;  // 1'000'000'000

  static constexpr int64_t kPriceScaleActual = 10'000; // price precision=4 (exchange)
  static constexpr int64_t kQtyScaleActual = 10;      // qty precision=1 (exchange)
  static constexpr int kPricePrecision = 6;
  static constexpr int kQtyPrecision = 3;
  static constexpr int kPricePrecisionActual = 4;
  static constexpr int kQtyPrecisionActual = 1;
};

struct BTCUSDCConfig {
  static constexpr int64_t kPriceScale = 10;
  static constexpr int64_t kQtyScale = 1000;
  static constexpr int64_t kPnlScale = kPriceScale * kQtyScale;  // 10'000
  static constexpr int64_t kPriceScaleActual = 10;
  static constexpr int64_t kQtyScaleActual = 1000;
  static constexpr int kPricePrecision = 1;
  static constexpr int kQtyPrecision = 3;
  static constexpr int kPricePrecisionActual = 1;
  static constexpr int kQtyPrecisionActual = 3;
};

#ifdef FIXED_POINT_SYMBOL_XRPUSDC
using FixedPointConfig = XRPUSDCConfig;
#else  // BTCUSDC (default)
using FixedPointConfig = BTCUSDCConfig;
#endif

static_assert(IFixedPointConfig<FixedPointConfig>, "Selected config is invalid!");

}  // namespace common

#endif  // FIXED_POINT_CONFIG_HPP
