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
#include <cmath>
#include <deque>
#include <vector>

namespace trading {

// === Configuration structure ===
struct RobustZScoreConfig {
  int window_size{30};
  int min_samples{20};
  double min_mad_threshold{5.0};

  // Volatility-adaptive threshold parameters
  int baseline_window{100};    // MAD baseline calculation window
  double min_vol_scalar{0.7};  // Minimum scaling (low volatility)
  double max_vol_scalar{1.3};  // Maximum scaling (high volatility)

  // Volatility ratio thresholds for adaptive scaling
  double vol_ratio_low{0.5};     // Low volatility threshold
  double vol_ratio_high{2.0};    // High volatility threshold
  int baseline_min_history{30};  // Minimum MAD history for baseline
};

/**
 * Robust Z-score calculator using Median and MAD (Median Absolute Deviation)
 *
 * Standard Z-score (Mean/StdDev) is vulnerable to outliers and fat-tail distributions
 * common in cryptocurrency markets. Robust Z-score uses:
 * - Median instead of Mean (resistant to outliers)
 * - MAD instead of StdDev (resistant to extreme values)
 *
 * Formula: Z_robust = (x - Median) / (MAD * 1.4826)
 * where MAD = Median(|x_i - Median(x)|)
 * and 1.4826 is the scale factor to match normal distribution std dev
 */
class RobustZScore {
 public:
  /**
   * @param config Configuration for window size, min samples, and MAD threshold
   */
  explicit RobustZScore(const RobustZScoreConfig& config)
      : window_size_(config.window_size),
        min_samples_(config.min_samples),
        min_mad_threshold_(config.min_mad_threshold),
        baseline_window_(config.baseline_window),
        min_vol_scalar_(config.min_vol_scalar),
        max_vol_scalar_(config.max_vol_scalar),
        vol_ratio_low_(config.vol_ratio_low),
        vol_ratio_high_(config.vol_ratio_high),
        baseline_min_history_(config.baseline_min_history) {
    // deque does not support reserve()
    // Pre-allocate sorting buffers to avoid heap allocation on every calculation
    sorted_prices_.reserve(config.window_size);
    abs_deviations_.reserve(config.window_size);
    // mad_history_ doesn't need reserve (deque)
  }

  /**
   * Feed a new price observation
   * @param price Current market price
   */
  void on_price(double price) {
    prices_.push_back(price);
    if (prices_.size() > static_cast<size_t>(window_size_)) {
      prices_.pop_front();
    }
  }

  /**
   * Calculate Robust Z-score for current price
   * @param current_price Price to evaluate
   * @return Z-score value (0.0 if insufficient samples)
   */
  double calculate_zscore(double current_price) const {
    if (prices_.size() < static_cast<size_t>(min_samples_)) {
      return 0.0;  // Insufficient data
    }

    double median = calculate_median();
    double mad = calculate_mad(median);

    // Track MAD history for baseline calculation
    mad_history_.push_back(mad);
    if (mad_history_.size() > static_cast<size_t>(baseline_window_)) {
      mad_history_.pop_front();
    }

    // Convert MAD to equivalent standard deviation scale
    const double scale_factor = 1.4826;
    double robust_std = mad * scale_factor;

    // Minimum MAD threshold to prevent extreme Z-scores during range-bound markets
    // When MAD is too small (e.g., 1.5-2.0), even tiny price movements create large Z-scores
    // BTC: 5.0 (equivalent to ~$7.4 std dev at $88k)
    // XRP: 0.0120 (equivalent to ~$0.018 std dev at $2.0)
    robust_std = std::max(robust_std, min_mad_threshold_);

    // Prevent division by zero (should not occur with min_mad_threshold)
    if (robust_std < 1e-8) {
      return 0.0;
    }

    return (current_price - median) / robust_std;
  }

  /**
   * Get current median of price window
   */
  double get_median() const { return calculate_median(); }

  /**
   * Get current MAD (Median Absolute Deviation)
   */
  double get_mad() const {
    if (prices_.size() < 2)
      return 0.0;
    double median = calculate_median();
    return calculate_mad(median);
  }

  /**
   * Get robust standard deviation (MAD * 1.4826)
   */
  double get_robust_std() const { return get_mad() * 1.4826; }

  /**
   * Get number of samples currently stored
   */
  size_t size() const { return prices_.size(); }

  /**
   * Get adaptive threshold based on current vs baseline volatility
   * @param base_threshold Base threshold (e.g., 2.0)
   * @return Volatility-adjusted threshold
   */
  double get_adaptive_threshold(double base_threshold) const {
    double baseline_mad = calculate_baseline_mad();
    double current_mad = get_mad();

    if (baseline_mad < 1e-8) {
      return base_threshold;  // Insufficient baseline, use fixed
    }

    double vol_ratio = current_mad / baseline_mad;

    // Clamp volatility scalar to [min_vol_scalar_, max_vol_scalar_]
    // Low volatility (vol_ratio < vol_ratio_low_) → lower threshold (capture small moves)
    // High volatility (vol_ratio > vol_ratio_high_) → higher threshold (avoid noise)
    double vol_range = vol_ratio_high_ - vol_ratio_low_;
    double vol_scalar = std::clamp(
        min_vol_scalar_ + (max_vol_scalar_ - min_vol_scalar_) *
                              (vol_ratio - vol_ratio_low_) / vol_range,
        min_vol_scalar_,
        max_vol_scalar_);

    return base_threshold * vol_scalar;
  }

 private:
  /**
   * Calculate median from current price window
   * Note: Uses pre-allocated buffer to avoid heap allocation
   */
  double calculate_median() const {
    if (prices_.empty())
      return 0.0;

    // Reuse pre-allocated buffer (mutable member)
    sorted_prices_.assign(prices_.begin(), prices_.end());
    std::sort(sorted_prices_.begin(), sorted_prices_.end());

    size_t mid = sorted_prices_.size() / 2;
    if (sorted_prices_.size() % 2 == 0) {
      return (sorted_prices_[mid - 1] + sorted_prices_[mid]) / 2.0;
    }
    return sorted_prices_[mid];
  }

  /**
   * Calculate MAD (Median Absolute Deviation)
   * @param median Pre-calculated median value
   */
  double calculate_mad(double median) const {
    if (prices_.size() < 2)
      return 0.0;

    // Reuse pre-allocated buffer (mutable member)
    abs_deviations_.clear();
    for (double price : prices_) {
      abs_deviations_.push_back(std::abs(price - median));
    }

    std::sort(abs_deviations_.begin(), abs_deviations_.end());

    size_t mid = abs_deviations_.size() / 2;
    if (abs_deviations_.size() % 2 == 0) {
      return (abs_deviations_[mid - 1] + abs_deviations_[mid]) / 2.0;
    }
    return abs_deviations_[mid];
  }

  /**
   * Calculate baseline MAD (rolling average of last N MAD values)
   */
  double calculate_baseline_mad() const {
    if (mad_history_.size() < static_cast<size_t>(baseline_min_history_)) {
      return get_mad();  // Insufficient history, use current
    }

    // Use last baseline_window_ MAD values for baseline
    size_t count =
        std::min(mad_history_.size(), static_cast<size_t>(baseline_window_));
    double sum = 0.0;

    for (size_t i = mad_history_.size() - count; i < mad_history_.size(); ++i) {
      sum += mad_history_[i];
    }

    return sum / count;
  }

  int window_size_;
  int min_samples_;
  double min_mad_threshold_;

  // Volatility-adaptive parameters
  int baseline_window_;
  double min_vol_scalar_;
  double max_vol_scalar_;
  double vol_ratio_low_;
  double vol_ratio_high_;
  int baseline_min_history_;

  std::deque<double> prices_;

  // MAD history for baseline calculation
  mutable std::deque<double> mad_history_;

  // Pre-allocated sorting buffers (mutable for use in const methods)
  mutable std::vector<double> sorted_prices_;
  mutable std::vector<double> abs_deviations_;
};

}  // namespace trading

#endif  // ROBUST_ZSCORE_H
