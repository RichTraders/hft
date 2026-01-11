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

// === Configuration structure (int64_t version) ===
struct RobustZScoreConfig {
  int window_size{30};
  int min_samples{20};
  int64_t min_mad_threshold_raw{50};  // In price scale (e.g., 5.0 * kPriceScale=10 = 50)

  // Volatility-adaptive threshold parameters
  int baseline_window{100};
  int64_t min_vol_scalar{7000};  // 0.7 * kSignalScale
  int64_t max_vol_scalar{13000}; // 1.3 * kSignalScale

  // Volatility ratio thresholds (scaled by kSignalScale)
  int64_t vol_ratio_low{5000};   // 0.5 * kSignalScale
  int64_t vol_ratio_high{20000}; // 2.0 * kSignalScale
  int baseline_min_history{30};
};

/**
 * Robust Z-score calculator using Median and MAD (Median Absolute Deviation)
 * Pure int64_t implementation for HFT hot path performance.
 *
 * Standard Z-score (Mean/StdDev) is vulnerable to outliers and fat-tail distributions
 * common in cryptocurrency markets. Robust Z-score uses:
 * - Median instead of Mean (resistant to outliers)
 * - MAD instead of StdDev (resistant to extreme values)
 *
 * Formula: Z_robust = (x - Median) * kZScoreScale / (MAD * 1.4826)
 * where MAD = Median(|x_i - Median(x)|)
 * and 1.4826 (kMadScaleFactor/10000) is the scale factor to match normal distribution std dev
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
        baseline_window_(config.baseline_window),
        min_vol_scalar_(config.min_vol_scalar),
        max_vol_scalar_(config.max_vol_scalar),
        vol_ratio_low_(config.vol_ratio_low),
        vol_ratio_high_(config.vol_ratio_high),
        baseline_min_history_(config.baseline_min_history) {
    sorted_prices_.reserve(config.window_size);
    abs_deviations_.reserve(config.window_size);
  }

  /**
   * Feed a new price observation (raw int64_t value from FixedPrice)
   * @param price_raw Price in FixedPrice scale (e.g., $87500.5 = 875005 with kPriceScale=10)
   */
  void on_price(int64_t price_raw) {
    prices_.push_back(price_raw);
    if (prices_.size() > static_cast<size_t>(window_size_)) {
      prices_.pop_front();
    }
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

    int64_t median = calculate_median();
    int64_t mad = calculate_mad(median);

    // Track MAD history for baseline calculation
    mad_history_.push_back(mad);
    if (mad_history_.size() > static_cast<size_t>(baseline_window_)) {
      mad_history_.pop_front();
    }

    // robust_std = mad * 1.4826
    // Using integer: robust_std_raw = mad * kMadScaleFactor / 10000
    int64_t robust_std = (mad * common::kMadScaleFactor) / 10000;
    robust_std = std::max(robust_std, min_mad_threshold_raw_);

    if (robust_std == 0) {
      return 0;
    }

    // Z-score = (current - median) * kZScoreScale / robust_std
    int64_t delta = current_price_raw - median;
    return (delta * common::kZScoreScale) / robust_std;
  }

  /**
   * Get current median of price window (raw value)
   */
  [[nodiscard]] int64_t get_median() const { return calculate_median(); }

  /**
   * Get current MAD (Median Absolute Deviation) in raw price scale
   */
  [[nodiscard]] int64_t get_mad() const {
    if (prices_.size() < 2) return 0;
    return calculate_mad(calculate_median());
  }

  /**
   * Get robust standard deviation (MAD * 1.4826) in raw price scale
   */
  [[nodiscard]] int64_t get_robust_std() const {
    return (get_mad() * common::kMadScaleFactor) / 10000;
  }

  [[nodiscard]] size_t size() const { return prices_.size(); }

  /**
   * Get adaptive threshold based on current vs baseline volatility
   * @param base_threshold_scaled Base threshold in kZScoreScale (e.g., 25000 for 2.5)
   * @return Volatility-adjusted threshold in kZScoreScale
   */
  [[nodiscard]] int64_t get_adaptive_threshold(int64_t base_threshold_scaled) const {
    int64_t baseline_mad = calculate_baseline_mad();
    int64_t current_mad = get_mad();

    if (baseline_mad == 0) {
      return base_threshold_scaled;
    }

    // vol_ratio = current_mad / baseline_mad (scaled by kSignalScale)
    int64_t vol_ratio = (current_mad * common::kSignalScale) / baseline_mad;

    // Clamp and interpolate volatility scalar
    int64_t vol_range = vol_ratio_high_ - vol_ratio_low_;
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
      vol_scalar = min_vol_scalar_ +
                   (max_vol_scalar_ - min_vol_scalar_) *
                       (vol_ratio - vol_ratio_low_) / vol_range;
    }

    // threshold = base * scalar / kSignalScale
    return (base_threshold_scaled * vol_scalar) / common::kSignalScale;
  }

 private:
  [[nodiscard]] int64_t calculate_median() const {
    if (prices_.empty()) return 0;

    sorted_prices_.assign(prices_.begin(), prices_.end());
    std::sort(sorted_prices_.begin(), sorted_prices_.end());

    size_t mid = sorted_prices_.size() / 2;
    if (sorted_prices_.size() % 2 == 0) {
      return (sorted_prices_[mid - 1] + sorted_prices_[mid]) / 2;
    }
    return sorted_prices_[mid];
  }

  [[nodiscard]] int64_t calculate_mad(int64_t median) const {
    if (prices_.size() < 2) return 0;

    abs_deviations_.clear();
    for (int64_t price : prices_) {
      abs_deviations_.push_back(std::abs(price - median));
    }

    std::sort(abs_deviations_.begin(), abs_deviations_.end());

    size_t mid = abs_deviations_.size() / 2;
    if (abs_deviations_.size() % 2 == 0) {
      return (abs_deviations_[mid - 1] + abs_deviations_[mid]) / 2;
    }
    return abs_deviations_[mid];
  }

  [[nodiscard]] int64_t calculate_baseline_mad() const {
    if (mad_history_.size() < static_cast<size_t>(baseline_min_history_)) {
      return get_mad();
    }

    size_t count = std::min(mad_history_.size(), static_cast<size_t>(baseline_window_));
    int64_t sum = 0;

    for (size_t i = mad_history_.size() - count; i < mad_history_.size(); ++i) {
      sum += mad_history_[i];
    }

    return sum / static_cast<int64_t>(count);
  }

  int window_size_;
  int min_samples_;
  int64_t min_mad_threshold_raw_;

  int baseline_window_;
  int64_t min_vol_scalar_;
  int64_t max_vol_scalar_;
  int64_t vol_ratio_low_;
  int64_t vol_ratio_high_;
  int baseline_min_history_;

  std::deque<int64_t> prices_;
  mutable std::deque<int64_t> mad_history_;
  mutable std::vector<int64_t> sorted_prices_;
  mutable std::vector<int64_t> abs_deviations_;
};

}  // namespace trading

#endif  // ROBUST_ZSCORE_H
