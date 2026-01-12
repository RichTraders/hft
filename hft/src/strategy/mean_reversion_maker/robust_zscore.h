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

#ifndef ROBUST_ZSCORE_H
#define ROBUST_ZSCORE_H

#include <algorithm>
#include <cstdint>
#include <deque>
#include <vector>
#include "common/fixed_point_config.hpp"

namespace trading {

// === Default configuration constants ===
namespace robust_zscore_defaults {
inline constexpr int kWindowSize = 30;
inline constexpr int kMinSamples = 20;
inline constexpr int64_t kMinMadThresholdRaw = 50;  // 5.0 * kPriceScale=10
// EMA alpha = 2 / (window + 1), for window=30: 2/31 ≈ 0.0645 → 645
inline constexpr int64_t kEmaAlpha = 645;
inline constexpr int kBaselineWindow = 100;
inline constexpr int kBaselineMinHistory = 30;
inline constexpr int64_t kMinVolScalar = 7000;   // 0.7 * kSignalScale
inline constexpr int64_t kMaxVolScalar = 13000;  // 1.3 * kSignalScale
inline constexpr int64_t kVolRatioLow = 5000;    // 0.5 * kSignalScale
inline constexpr int64_t kVolRatioHigh = 20000;  // 2.0 * kSignalScale
// MAD to StdDev conversion factor: 1.4826 scaled by 10000
inline constexpr int64_t kMadScaleFactor = 14826;
inline constexpr int64_t kMadScaleDivisor = 10000;
}  // namespace robust_zscore_defaults

// === Configuration structure (int64_t version) ===
struct RobustZScoreConfig {
  int window_size{robust_zscore_defaults::kWindowSize};
  int min_samples{robust_zscore_defaults::kMinSamples};
  int64_t min_mad_threshold_raw{robust_zscore_defaults::kMinMadThresholdRaw};
  int64_t ema_alpha{robust_zscore_defaults::kEmaAlpha};

  // Volatility-adaptive threshold parameters
  int baseline_window{robust_zscore_defaults::kBaselineWindow};
  int64_t min_vol_scalar{robust_zscore_defaults::kMinVolScalar};
  int64_t max_vol_scalar{robust_zscore_defaults::kMaxVolScalar};

  // Volatility ratio thresholds (scaled by kSignalScale)
  int64_t vol_ratio_low{robust_zscore_defaults::kVolRatioLow};
  int64_t vol_ratio_high{robust_zscore_defaults::kVolRatioHigh};
  int baseline_min_history{robust_zscore_defaults::kBaselineMinHistory};
};

/**
 * Robust Z-score calculator using Median and EMAD (Exponential Moving Average Deviation)
 * Pure int64_t implementation for HFT hot path performance.
 *
 * Standard Z-score (Mean/StdDev) is vulnerable to outliers and fat-tail distributions
 * common in cryptocurrency markets. This implementation uses:
 * - Median for center estimation (resistant to outliers)
 * - EMAD for dispersion (O(1) incremental update vs O(n log n) MAD)
 *
 * EMAD is updated incrementally:
 *   emad = alpha * |price - ema_price| + (1 - alpha) * emad
 *
 * Formula: Z_robust = (x - Median) * kZScoreScale / (EMAD * 1.4826)
 * where 1.4826 (kMadScaleFactor/10000) scales to match normal distribution std dev
 *
 * Returns Z-score scaled by kZScoreScale (10000):
 * - Z-score of 2.5 returns 25000
 * - Z-score of -1.8 returns -18000
 */
class RobustZScore {
 public:
  explicit RobustZScore(const RobustZScoreConfig& config)
      : window_size_(config.window_size),
        min_samples_(config.min_samples),
        min_mad_threshold_raw_(config.min_mad_threshold_raw),
        ema_alpha_(config.ema_alpha),
        baseline_window_(config.baseline_window),
        min_vol_scalar_(config.min_vol_scalar),
        max_vol_scalar_(config.max_vol_scalar),
        vol_ratio_low_(config.vol_ratio_low),
        vol_ratio_high_(config.vol_ratio_high),
        baseline_min_history_(config.baseline_min_history) {
    sorted_prices_.reserve(config.window_size);
  }

  /**
   * Feed a new price observation (raw int64_t value from FixedPrice)
   * Updates sorted vector incrementally: O(log n) search + O(n) insert/remove
   * Updates EMA price and EMAD incrementally in O(1)
   * @param price_raw Price in FixedPrice scale (e.g., $87500.5 = 875005 with kPriceScale=10)
   */
  void on_price(int64_t price_raw) {
    // Track insertion order for sliding window
    prices_.push_back(price_raw);

    // Binary search insert into sorted vector - O(log n) search + O(n) memmove
    auto insert_pos = std::lower_bound(sorted_prices_.begin(),
        sorted_prices_.end(),
        price_raw);
    sorted_prices_.insert(insert_pos, price_raw);

    // Remove oldest element if window full
    if (prices_.size() > static_cast<size_t>(window_size_)) {
      const int64_t old_val = prices_.front();
      prices_.pop_front();

      // Binary search remove from sorted vector - O(log n) search + O(n) memmove
      auto remove_pos = std::lower_bound(sorted_prices_.begin(),
          sorted_prices_.end(),
          old_val);
      sorted_prices_.erase(remove_pos);
    }

    // Update EMA price and EMAD
    if (sample_count_ == 0) {
      ema_price_ = price_raw;
      emad_ = 0;
    } else {
      // EMAD = alpha * |price - ema_price| + (1 - alpha) * EMAD
      const int64_t deviation = std::abs(price_raw - ema_price_);
      emad_ =
          (ema_alpha_ * deviation + (common::kEmaScale - ema_alpha_) * emad_) /
          common::kEmaScale;

      // EMA price update
      ema_price_ = (ema_alpha_ * price_raw +
                       (common::kEmaScale - ema_alpha_) * ema_price_) /
                   common::kEmaScale;
    }
    ++sample_count_;
  }

  /**
   * Calculate Robust Z-score for current price
   * @param current_price_raw Price in FixedPrice scale
   * @return Z-score scaled by kZScoreScale (25000 = 2.5, -18000 = -1.8), 0 if insufficient data
   */
  [[nodiscard]] int64_t calculate_zscore(int64_t current_price_raw) const {
    if (prices_.size() < static_cast<size_t>(min_samples_)) {
      return 0;
    }

    const int64_t median = calculate_median();

    // Track EMAD history for baseline calculation with running sum
    emad_sum_ += emad_;
    emad_history_.push_back(emad_);
    if (emad_history_.size() > static_cast<size_t>(baseline_window_)) {
      emad_sum_ -= emad_history_.front();
      emad_history_.pop_front();
    }

    // robust_std = emad * 1.4826
    // Using integer: robust_std_raw = emad * kMadScaleFactor / kMadScaleDivisor
    int64_t robust_std = (emad_ * robust_zscore_defaults::kMadScaleFactor) /
                         robust_zscore_defaults::kMadScaleDivisor;
    robust_std = std::max(robust_std, min_mad_threshold_raw_);

    if (robust_std == 0) {
      return 0;
    }

    // Z-score = (current - median) * kZScoreScale / robust_std
    const int64_t delta = current_price_raw - median;
    return (delta * common::kZScoreScale) / robust_std;
  }

  /**
   * Get current median of price window (raw value)
   */
  [[nodiscard]] int64_t get_median() const { return calculate_median(); }

  /**
   * Get current EMAD (Exponential Moving Average Deviation) in raw price scale
   * O(1) - returns cached value
   */
  [[nodiscard]] int64_t get_mad() const { return emad_; }

  /**
   * Get EMA price (for debugging/monitoring)
   */
  [[nodiscard]] int64_t get_ema_price() const { return ema_price_; }

  /**
   * Get robust standard deviation (EMAD * 1.4826) in raw price scale
   */
  [[nodiscard]] int64_t get_robust_std() const {
    return (emad_ * robust_zscore_defaults::kMadScaleFactor) /
           robust_zscore_defaults::kMadScaleDivisor;
  }

  [[nodiscard]] size_t size() const { return prices_.size(); }

  /**
   * Get adaptive threshold based on current vs baseline volatility
   * @param base_threshold_scaled Base threshold in kZScoreScale (e.g., 25000 for 2.5)
   * @return Volatility-adjusted threshold in kZScoreScale
   */
  [[nodiscard]] int64_t get_adaptive_threshold(
      int64_t base_threshold_scaled) const {
    const int64_t baseline_emad = calculate_baseline_emad();
    const int64_t current_emad = emad_;

    if (baseline_emad == 0) {
      return base_threshold_scaled;
    }

    // vol_ratio = current_emad / baseline_emad (scaled by kSignalScale)
    const int64_t vol_ratio =
        (current_emad * common::kSignalScale) / baseline_emad;

    // Clamp and interpolate volatility scalar
    const int64_t vol_range = vol_ratio_high_ - vol_ratio_low_;
    if (vol_range == 0) {
      return base_threshold_scaled;
    }

    // vol_scalar = min + (max - min) * (ratio - low) / range
    int64_t vol_scalar;
    if (vol_ratio <= vol_ratio_low_) {
      vol_scalar = min_vol_scalar_;
    } else if (vol_ratio >= vol_ratio_high_) {
      vol_scalar = max_vol_scalar_;
    } else {
      vol_scalar = min_vol_scalar_ + (max_vol_scalar_ - min_vol_scalar_) *
                                         (vol_ratio - vol_ratio_low_) /
                                         vol_range;
    }

    // threshold = base * scalar / kSignalScale
    return (base_threshold_scaled * vol_scalar) / common::kSignalScale;
  }

 private:
  // O(1) median access - sorted_prices_ is always maintained in sorted order
  [[nodiscard]] int64_t calculate_median() const {
    if (sorted_prices_.empty()) [[unlikely]] {
      return 0;
    }

    const size_t mid = sorted_prices_.size() / 2;
    if (sorted_prices_.size() % 2 == 0) {
      return (sorted_prices_[mid - 1] + sorted_prices_[mid]) / 2;
    }
    return sorted_prices_[mid];
  }

  [[nodiscard]] int64_t calculate_baseline_emad() const {
    if (emad_history_.size() < static_cast<size_t>(baseline_min_history_)) {
      return emad_;
    }

    // O(1) average using running sum
    const size_t count =
        std::min(emad_history_.size(), static_cast<size_t>(baseline_window_));
    return emad_sum_ / static_cast<int64_t>(count);
  }

  const int window_size_;
  const int min_samples_;
  const int64_t min_mad_threshold_raw_;
  const int64_t ema_alpha_;

  const int baseline_window_;
  const int64_t min_vol_scalar_;
  const int64_t max_vol_scalar_;
  const int64_t vol_ratio_low_;
  const int64_t vol_ratio_high_;
  const int baseline_min_history_;

  // Sliding window for median - prices_ tracks insertion order, sorted_prices_ is always sorted
  std::deque<int64_t> prices_;
  std::vector<int64_t> sorted_prices_;

  // EMAD state (O(1) update)
  int64_t ema_price_{0};
  int64_t emad_{0};
  int sample_count_{0};

  // EMAD history for baseline calculation
  mutable std::deque<int64_t> emad_history_;
  mutable int64_t emad_sum_{0};  // Running sum for O(1) average
};

}  // namespace trading

#endif  // ROBUST_ZSCORE_H
