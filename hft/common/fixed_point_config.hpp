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
  { T::kPriceScale } -> std::convertible_to<int64_t>;
  { T::kQtyScale } -> std::convertible_to<int64_t>;
  { T::kPnlScale } -> std::convertible_to<int64_t>;

  { T::kPricePrecision } -> std::convertible_to<int>;
  { T::kQtyPrecision } -> std::convertible_to<int>;

  requires T::kPriceScale > 0;
};

// symbol (Select in CMake: -DFIXED_POINT_SYMBOL=BTCUSDC)

struct XRPUSDCConfig {
  static constexpr int64_t kPriceScale =
      1'000'000;                               // price precision=6 (internal)
  static constexpr int64_t kQtyScale = 1'000;  // qty precision=3 (internal)
  static constexpr int64_t kPnlScale =
      kPriceScale * kQtyScale;  // 1'000'000'000

  static constexpr int64_t kPriceScaleActual =
      10'000;  // price precision=4 (exchange)
  static constexpr int64_t kQtyScaleActual = 10;  // qty precision=1 (exchange)
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

struct BTCUSDTConfig {
  static constexpr int64_t kPriceScale = 100;   // price precision=2
  static constexpr int64_t kQtyScale = 100000;  // qty precision=5
  static constexpr int64_t kPnlScale = kPriceScale * kQtyScale;  // 10'000'000
  static constexpr int64_t kPriceScaleActual = 100;
  static constexpr int64_t kQtyScaleActual = 100000;
  static constexpr int kPricePrecision = 2;
  static constexpr int kQtyPrecision = 5;
  static constexpr int kPricePrecisionActual = 2;
  static constexpr int kQtyPrecisionActual = 5;
};

#ifdef FIXED_POINT_SYMBOL_XRPUSDC
using FixedPointConfig = XRPUSDCConfig;
#elif defined(FIXED_POINT_SYMBOL_BTCUSDT)
using FixedPointConfig = BTCUSDTConfig;
#else  // BTCUSDC (default)
using FixedPointConfig = BTCUSDCConfig;
#endif

static_assert(IFixedPointConfig<FixedPointConfig>,
    "Selected config is invalid!");

// =========================================
// Common Scales for Strategy Calculations
// =========================================

// Z-score scale: 2.5 → 25000 (4 decimal precision)
static constexpr int64_t kZScoreScale = 10000;

// Signal score scale: 0.65 → 6500 (for normalized [0,1] values)
static constexpr int64_t kSignalScale = 10000;

// Basis points scale: 0.15% (0.0015) → 15
// Note: 1 bp = 0.01% = 0.0001, so 15 bps = 0.15%
static constexpr int64_t kBpsScale = 10000;

// OBI (Order Book Imbalance) scale: 0.25 → 2500 (already exists as kObiScale in feature_engine)
static constexpr int64_t kObiScale = 10000;

// EMA alpha scale: 0.03 → 300
static constexpr int64_t kEmaScale = 10000;

}  // namespace common

#endif  // FIXED_POINT_CONFIG_HPP
