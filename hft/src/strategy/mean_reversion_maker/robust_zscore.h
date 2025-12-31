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
   * @param window_size Number of recent prices to track (e.g., 30 ticks)
   * @param min_samples Minimum samples required before calculating Z-score
   * @param min_mad_threshold Minimum MAD threshold to prevent extreme Z-scores (default: 5.0 for BTC)
   */
  RobustZScore(int window_size, int min_samples, double min_mad_threshold = 5.0)
      : window_size_(window_size),
        min_samples_(min_samples),
        min_mad_threshold_(min_mad_threshold) {
    // deque does not support reserve()
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

    // Convert MAD to equivalent standard deviation scale
    const double scale_factor = 1.4826;
    double robust_std = mad * scale_factor;

    // Minimum MAD threshold to prevent extreme Z-scores during range-bound markets
    // When MAD is too small (e.g., 1.5-2.0), even tiny price movements create large Z-scores
    // BTC: 5.0 (equivalent to ~$7.4 std dev at $88k)
    // XRP: 0.01 (equivalent to ~$0.015 std dev at $2.5)
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

 private:
  /**
   * Calculate median from current price window
   * Note: This creates a sorted copy, leaving original deque unchanged
   */
  double calculate_median() const {
    if (prices_.empty())
      return 0.0;

    std::vector<double> sorted(prices_.begin(), prices_.end());
    std::sort(sorted.begin(), sorted.end());

    size_t mid = sorted.size() / 2;
    if (sorted.size() % 2 == 0) {
      return (sorted[mid - 1] + sorted[mid]) / 2.0;
    }
    return sorted[mid];
  }

  /**
   * Calculate MAD (Median Absolute Deviation)
   * @param median Pre-calculated median value
   */
  double calculate_mad(double median) const {
    if (prices_.size() < 2)
      return 0.0;

    std::vector<double> abs_deviations;
    abs_deviations.reserve(prices_.size());

    for (double price : prices_) {
      abs_deviations.push_back(std::abs(price - median));
    }

    std::sort(abs_deviations.begin(), abs_deviations.end());

    size_t mid = abs_deviations.size() / 2;
    if (abs_deviations.size() % 2 == 0) {
      return (abs_deviations[mid - 1] + abs_deviations[mid]) / 2.0;
    }
    return abs_deviations[mid];
  }

  int window_size_;
  int min_samples_;
  double min_mad_threshold_;
  std::deque<double> prices_;
};

}  // namespace trading

#endif  // ROBUST_ZSCORE_H
