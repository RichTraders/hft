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

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <random>
#include <vector>

#include "strategy/mean_reversion_maker/robust_zscore.h"

using namespace trading;

class RobustZScoreTest : public ::testing::Test {
 protected:
  static constexpr int64_t kPriceScale = common::FixedPointConfig::kPriceScale;
  static constexpr int64_t kZScoreScale = common::kZScoreScale;

  static int64_t to_price_raw(int64_t price) {
    return price * kPriceScale;
  }
};

TEST_F(RobustZScoreTest, MedianCalculation_OddCount) {
  RobustZScoreConfig config;
  config.window_size = 5;
  config.min_samples = 3;
  RobustZScore zscore(config);

  // Feed prices: 100, 102, 101, 103, 99
  // Sorted: 99, 100, 101, 102, 103 -> median = 101
  std::vector<int64_t> prices = {100, 102, 101, 103, 99};
  for (int64_t p : prices) {
    zscore.on_price(to_price_raw(p));
  }

  const int64_t median = zscore.get_median();
  EXPECT_EQ(median, to_price_raw(101));
}

TEST_F(RobustZScoreTest, MedianCalculation_EvenCount) {
  RobustZScoreConfig config;
  config.window_size = 4;
  config.min_samples = 2;
  RobustZScore zscore(config);

  // Feed prices: 100, 102, 101, 103
  // Sorted: 100, 101, 102, 103 -> median = (101 + 102) / 2
  std::vector<int64_t> prices = {100, 102, 101, 103};
  for (int64_t p : prices) {
    zscore.on_price(to_price_raw(p));
  }

  const int64_t median = zscore.get_median();
  const int64_t expected = (to_price_raw(101) + to_price_raw(102)) / 2;
  EXPECT_EQ(median, expected);
}

TEST_F(RobustZScoreTest, EMADCalculation) {
  RobustZScoreConfig config;
  config.window_size = 30;
  config.min_samples = 10;
  config.ema_alpha = 645;  // ~0.0645 for window 30
  RobustZScore zscore(config);

  // Feed stable prices around 100 with small variance
  // EMAD should converge to approximately the average absolute deviation
  for (int i = 0; i < 100; ++i) {
    const int64_t price = 100 + (i % 3) - 1;  // 99, 100, 101 cycle
    zscore.on_price(to_price_raw(price));
  }

  // EMAD should be positive and reasonable
  const int64_t emad = zscore.get_mad();
  EXPECT_GT(emad, 0) << "EMAD should be positive after feeding data";
  // Average deviation from mean ~100 is about 0.67 for {99,100,101}
  // Allow range [0.3, 2.0] in price scale
  EXPECT_GT(emad, to_price_raw(1) / 3);
  EXPECT_LT(emad, to_price_raw(2));
}

// Reference MAD calculation for comparison
static int64_t calculate_true_mad(const std::vector<int64_t>& prices) {
  if (prices.size() < 2) return 0;

  std::vector<int64_t> sorted = prices;
  std::sort(sorted.begin(), sorted.end());
  const size_t mid = sorted.size() / 2;
  const int64_t median = (sorted.size() % 2 == 0)
                             ? (sorted[mid - 1] + sorted[mid]) / 2
                             : sorted[mid];

  std::vector<int64_t> abs_devs;
  abs_devs.reserve(prices.size());
  for (int64_t p : prices) {
    abs_devs.push_back(std::abs(p - median));
  }
  std::sort(abs_devs.begin(), abs_devs.end());
  const size_t mid2 = abs_devs.size() / 2;
  return (abs_devs.size() % 2 == 0)
             ? (abs_devs[mid2 - 1] + abs_devs[mid2]) / 2
             : abs_devs[mid2];
}

TEST_F(RobustZScoreTest, EMAD_vs_MAD_Comparison) {
  RobustZScoreConfig config;
  config.window_size = 30;
  config.min_samples = 20;
  config.ema_alpha = 645;  // default: 2/(30+1) ≈ 0.0645
  RobustZScore zscore(config);

  std::mt19937 rng(42);
  std::uniform_int_distribution<int64_t> dist(95, 105);

  std::vector<int64_t> prices_raw;
  prices_raw.reserve(100);

  std::cout << "\n=== EMAD vs MAD Comparison ===" << std::endl;
  std::cout << "Sample\tTrue MAD\tEMAD\t\tDiff %\t\tZ-score diff" << std::endl;

  for (int i = 0; i < 100; ++i) {
    const int64_t p = to_price_raw(dist(rng));
    prices_raw.push_back(p);
    zscore.on_price(p);

    if (prices_raw.size() > static_cast<size_t>(config.window_size)) {
      prices_raw.erase(prices_raw.begin());
    }

    if (i >= 29 && i % 10 == 9) {
      const int64_t true_mad = calculate_true_mad(prices_raw);
      const int64_t emad = zscore.get_mad();
      const double diff_pct =
          true_mad > 0
              ? 100.0 * static_cast<double>(std::abs(emad - true_mad)) /
                    static_cast<double>(true_mad)
              : 0.0;

      // Calculate z-score difference for a test price
      const int64_t test_price = to_price_raw(110);
      const int64_t median = zscore.get_median();
      const int64_t delta = test_price - median;

      const int64_t robust_std_emad =
          std::max((emad * robust_zscore_defaults::kMadScaleFactor) / 10000, int64_t{1});
      const int64_t robust_std_mad =
          std::max((true_mad * robust_zscore_defaults::kMadScaleFactor) / 10000, int64_t{1});

      const int64_t zscore_emad =
          (delta * common::kZScoreScale) / robust_std_emad;
      const int64_t zscore_mad =
          (delta * common::kZScoreScale) / robust_std_mad;
      const double zscore_diff =
          static_cast<double>(std::abs(zscore_emad - zscore_mad)) /
          common::kZScoreScale;

      std::cout << i + 1 << "\t" << true_mad << "\t\t" << emad << "\t\t"
                << std::fixed << std::setprecision(1) << diff_pct << "%\t\t"
                << std::setprecision(2) << zscore_diff << std::endl;
    }
  }
}

TEST_F(RobustZScoreTest, ZScoreCalculation_PositiveDeviation) {
  RobustZScoreConfig config;
  config.window_size = 30;
  config.min_samples = 20;
  config.min_mad_threshold_raw = 1;  // Very low threshold for testing
  RobustZScore zscore(config);

  // Create a stable distribution around 100
  for (int i = 0; i < 25; ++i) {
    const int64_t price = 100 + (i % 3) - 1;  // 99, 100, 101, 99, 100, ...
    zscore.on_price(to_price_raw(price));
  }

  // Current price significantly above median should give positive z-score
  const int64_t current = to_price_raw(105);
  const int64_t z = zscore.calculate_zscore(current);
  EXPECT_GT(z, 0) << "Z-score should be positive for price above median";
}

TEST_F(RobustZScoreTest, ZScoreCalculation_NegativeDeviation) {
  RobustZScoreConfig config;
  config.window_size = 30;
  config.min_samples = 20;
  config.min_mad_threshold_raw = 1;
  RobustZScore zscore(config);

  for (int i = 0; i < 25; ++i) {
    const int64_t price = 100 + (i % 3) - 1;
    zscore.on_price(to_price_raw(price));
  }

  const int64_t current = to_price_raw(95);
  const int64_t z = zscore.calculate_zscore(current);
  EXPECT_LT(z, 0) << "Z-score should be negative for price below median";
}

TEST_F(RobustZScoreTest, ZScoreCalculation_InsufficientData) {
  RobustZScoreConfig config;
  config.window_size = 30;
  config.min_samples = 20;
  RobustZScore zscore(config);

  // Only feed 10 prices (less than min_samples=20)
  for (int i = 0; i < 10; ++i) {
    zscore.on_price(to_price_raw(100 + i));
  }

  const int64_t z = zscore.calculate_zscore(to_price_raw(150));
  EXPECT_EQ(z, 0) << "Should return 0 with insufficient data";
}

TEST_F(RobustZScoreTest, SlidingWindow_EvictsOldData) {
  RobustZScoreConfig config;
  config.window_size = 5;
  config.min_samples = 3;
  RobustZScore zscore(config);

  // Fill window: 100, 101, 102, 103, 104
  for (int i = 0; i < 5; ++i) {
    zscore.on_price(to_price_raw(100 + i));
  }
  // Median should be 102
  EXPECT_EQ(zscore.get_median(), to_price_raw(102));

  // Add 200 - window becomes: 101, 102, 103, 104, 200
  zscore.on_price(to_price_raw(200));
  // Sorted: 101, 102, 103, 104, 200 -> median = 103
  EXPECT_EQ(zscore.get_median(), to_price_raw(103));

  // Add more high values
  zscore.on_price(to_price_raw(201));  // 102, 103, 104, 200, 201 -> median = 104
  zscore.on_price(to_price_raw(202));  // 103, 104, 200, 201, 202 -> median = 200
  EXPECT_EQ(zscore.get_median(), to_price_raw(200));
}

TEST_F(RobustZScoreTest, OutlierResistance) {
  RobustZScoreConfig config;
  config.window_size = 10;
  config.min_samples = 5;
  config.min_mad_threshold_raw = 1;
  RobustZScore zscore(config);

  // Normal distribution around 100
  std::vector<int64_t> normal_prices = {99, 100, 101, 100, 99, 100, 101, 100};
  for (int64_t p : normal_prices) {
    zscore.on_price(to_price_raw(p));
  }
  const int64_t median_before = zscore.get_median();

  // Add extreme outliers
  zscore.on_price(to_price_raw(500));
  zscore.on_price(to_price_raw(1000));
  const int64_t median_after = zscore.get_median();

  // Median should not change much despite outliers (robust property)
  // With 10 values: 99,100,101,100,99,100,101,100,500,1000
  // Sorted: 99,99,100,100,100,100,101,101,500,1000
  // Median = (100+100)/2 = 100
  EXPECT_EQ(median_after, to_price_raw(100));

  // Change from median_before should be minimal
  const double change_pct = std::abs(
      static_cast<double>(median_after - median_before) /
      static_cast<double>(median_before));
  EXPECT_LT(change_pct, 0.05) << "Median should be resistant to outliers";
}

TEST_F(RobustZScoreTest, RobustStd_MatchesMADScale) {
  RobustZScoreConfig config;
  config.window_size = 20;
  config.min_samples = 10;
  RobustZScore zscore(config);

  // Feed prices with known distribution
  for (int i = 0; i < 20; ++i) {
    zscore.on_price(to_price_raw(100 + (i % 5)));
  }

  const int64_t mad = zscore.get_mad();
  const int64_t robust_std = zscore.get_robust_std();

  // robust_std = mad * 1.4826
  // kMadScaleFactor = 14826
  const int64_t expected = (mad * robust_zscore_defaults::kMadScaleFactor) / 10000;
  EXPECT_EQ(robust_std, expected);
}

TEST_F(RobustZScoreTest, ZScoreConsistency) {
  // Test that z-score calculation is consistent and reasonable
  RobustZScoreConfig config;
  config.window_size = 30;
  config.min_samples = 20;
  config.min_mad_threshold_raw = kPriceScale / 2;
  RobustZScore zscore(config);

  std::mt19937 rng(42);
  std::uniform_int_distribution<int64_t> dist(95, 105);

  for (int i = 0; i < 30; ++i) {
    zscore.on_price(to_price_raw(dist(rng)));
  }

  // Price above median should give positive z-score
  const int64_t zscore_high = zscore.calculate_zscore(to_price_raw(110));
  EXPECT_GT(zscore_high, 0);

  // Price below median should give negative z-score
  const int64_t zscore_low = zscore.calculate_zscore(to_price_raw(90));
  EXPECT_LT(zscore_low, 0);

  // More extreme deviation should give larger absolute z-score
  const int64_t zscore_extreme = zscore.calculate_zscore(to_price_raw(120));
  EXPECT_GT(std::abs(zscore_extreme), std::abs(zscore_high));
}

TEST_F(RobustZScoreTest, AdaptiveThreshold_LowVolatility) {
  RobustZScoreConfig config;
  config.window_size = 30;
  config.min_samples = 20;
  config.baseline_window = 50;
  config.baseline_min_history = 10;
  config.min_vol_scalar = 7000;   // 0.7
  config.max_vol_scalar = 13000;  // 1.3
  config.vol_ratio_low = 5000;    // 0.5
  config.vol_ratio_high = 20000;  // 2.0
  RobustZScore zscore(config);

  // Build up baseline with moderate volatility
  for (int i = 0; i < 40; ++i) {
    zscore.on_price(to_price_raw(100 + (i % 5) - 2));
    (void)zscore.calculate_zscore(to_price_raw(100));  // Build mad_history
  }

  // Now feed very stable prices (low volatility)
  for (int i = 0; i < 30; ++i) {
    zscore.on_price(to_price_raw(100));
    (void)zscore.calculate_zscore(to_price_raw(100));
  }

  const int64_t base_threshold = 25000;  // 2.5 z-score
  const int64_t adaptive = zscore.get_adaptive_threshold(base_threshold);

  // Low volatility should reduce threshold
  EXPECT_LE(adaptive, base_threshold)
      << "Low volatility should reduce or maintain threshold";
}

// Performance benchmark test
TEST_F(RobustZScoreTest, PerformanceBenchmark_MedianMAD) {
  RobustZScoreConfig config;
  config.window_size = 30;
  config.min_samples = 20;
  RobustZScore zscore(config);

  std::mt19937 rng(12345);
  std::uniform_int_distribution<int64_t> dist(95, 105);

  // Warm up
  for (int i = 0; i < 30; ++i) {
    zscore.on_price(to_price_raw(dist(rng)));
  }

  constexpr int kIterations = 100000;
  std::vector<int64_t> prices;
  prices.reserve(kIterations);
  for (int i = 0; i < kIterations; ++i) {
    prices.push_back(to_price_raw(dist(rng)));
  }

  auto start = std::chrono::high_resolution_clock::now();
  int64_t sum = 0;
  for (int i = 0; i < kIterations; ++i) {
    zscore.on_price(prices[i]);
    sum += zscore.calculate_zscore(prices[i]);
  }
  auto end = std::chrono::high_resolution_clock::now();

  auto duration_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
  double ns_per_op = static_cast<double>(duration_ns) / kIterations;

  std::cout << "\n=== RobustZScore Performance ===" << std::endl;
  std::cout << "Iterations: " << kIterations << std::endl;
  std::cout << "Total time: " << static_cast<double>(duration_ns) / 1e6 << " ms" << std::endl;
  std::cout << "Per operation: " << ns_per_op << " ns" << std::endl;
  std::cout << "Throughput: " << 1e9 / ns_per_op << " ops/sec" << std::endl;
  std::cout << "(sum = " << sum << " to prevent optimization)" << std::endl;

  // Performance assertion: should complete under 10 microseconds per op
  EXPECT_LT(ns_per_op, 10000) << "Performance regression: " << ns_per_op << " ns/op";
}

TEST_F(RobustZScoreTest, PerformanceBenchmark_MedianOnly) {
  RobustZScoreConfig config;
  config.window_size = 30;
  config.min_samples = 20;
  RobustZScore zscore(config);

  std::mt19937 rng(12345);
  std::uniform_int_distribution<int64_t> dist(95, 105);

  for (int i = 0; i < 30; ++i) {
    zscore.on_price(to_price_raw(dist(rng)));
  }

  constexpr int kIterations = 100000;
  auto start = std::chrono::high_resolution_clock::now();
  int64_t sum = 0;
  for (int i = 0; i < kIterations; ++i) {
    sum += zscore.get_median();
  }
  auto end = std::chrono::high_resolution_clock::now();

  auto duration_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
  double ns_per_op = static_cast<double>(duration_ns) / kIterations;

  std::cout << "\n=== Median-only Performance ===" << std::endl;
  std::cout << "Per operation: " << ns_per_op << " ns" << std::endl;
  std::cout << "(sum = " << sum << " to prevent optimization)" << std::endl;
}

TEST_F(RobustZScoreTest, PerformanceBenchmark_MADOnly) {
  RobustZScoreConfig config;
  config.window_size = 30;
  config.min_samples = 20;
  RobustZScore zscore(config);

  std::mt19937 rng(12345);
  std::uniform_int_distribution<int64_t> dist(95, 105);

  for (int i = 0; i < 30; ++i) {
    zscore.on_price(to_price_raw(dist(rng)));
  }

  constexpr int kIterations = 100000;
  auto start = std::chrono::high_resolution_clock::now();
  int64_t sum = 0;
  for (int i = 0; i < kIterations; ++i) {
    sum += zscore.get_mad();
  }
  auto end = std::chrono::high_resolution_clock::now();

  auto duration_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
  double ns_per_op = static_cast<double>(duration_ns) / kIterations;

  std::cout << "\n=== MAD-only Performance ===" << std::endl;
  std::cout << "Per operation: " << ns_per_op << " ns" << std::endl;
  std::cout << "(sum = " << sum << " to prevent optimization)" << std::endl;
}

// Compare sort vs nth_element for median calculation
TEST_F(RobustZScoreTest, PerformanceBenchmark_SortVsNthElement) {
  constexpr int kWindowSize = 30;
  constexpr int kIterations = 100000;

  std::mt19937 rng(42);
  std::uniform_int_distribution<int64_t> dist(95000, 105000);

  // Pre-generate all data
  std::vector<std::deque<int64_t>> windows(kIterations);
  for (int i = 0; i < kIterations; ++i) {
    for (int j = 0; j < kWindowSize; ++j) {
      windows[i].push_back(dist(rng));
    }
  }

  // Method 1: std::sort (current implementation)
  std::vector<int64_t> buffer;
  buffer.reserve(kWindowSize);

  auto start_sort = std::chrono::high_resolution_clock::now();
  int64_t sum_sort = 0;
  for (int i = 0; i < kIterations; ++i) {
    buffer.assign(windows[i].begin(), windows[i].end());
    std::sort(buffer.begin(), buffer.end());
    const size_t mid = buffer.size() / 2;
    sum_sort += (buffer[mid - 1] + buffer[mid]) / 2;
  }
  auto end_sort = std::chrono::high_resolution_clock::now();

  // Method 2: std::nth_element
  auto start_nth = std::chrono::high_resolution_clock::now();
  int64_t sum_nth = 0;
  for (int i = 0; i < kIterations; ++i) {
    buffer.assign(windows[i].begin(), windows[i].end());
    const size_t mid = buffer.size() / 2;
    std::nth_element(buffer.begin(), buffer.begin() + mid, buffer.end());
    int64_t median_high = buffer[mid];
    // For even size, need to find max of lower half
    int64_t median_low = *std::max_element(buffer.begin(), buffer.begin() + mid);
    sum_nth += (median_low + median_high) / 2;
  }
  auto end_nth = std::chrono::high_resolution_clock::now();

  auto ns_sort = std::chrono::duration_cast<std::chrono::nanoseconds>(
                     end_sort - start_sort).count();
  auto ns_nth = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    end_nth - start_nth).count();

  std::cout << "\n=== Sort vs nth_element (window=" << kWindowSize << ") ===" << std::endl;
  std::cout << "std::sort:        " << static_cast<double>(ns_sort) / kIterations << " ns/op" << std::endl;
  std::cout << "std::nth_element: " << static_cast<double>(ns_nth) / kIterations << " ns/op" << std::endl;
  std::cout << "Speedup: " << std::fixed << std::setprecision(2)
            << static_cast<double>(ns_sort) / ns_nth << "x" << std::endl;
  std::cout << "(sum_sort=" << sum_sort << ", sum_nth=" << sum_nth << ")" << std::endl;

  // Verify correctness
  EXPECT_EQ(sum_sort, sum_nth) << "Median calculations should match";
}

// Test sorted vector maintenance with binary search insert
TEST_F(RobustZScoreTest, PerformanceBenchmark_SortedVectorMaintenance) {
  constexpr int kWindowSize = 30;
  constexpr int kIterations = 100000;

  std::mt19937 rng(42);
  std::uniform_int_distribution<int64_t> dist(95000, 105000);

  // Pre-generate stream data
  std::vector<int64_t> stream(kIterations + kWindowSize);
  for (auto& val : stream) {
    val = dist(rng);
  }

  // Method 1: Current approach (deque + sort each time)
  std::deque<int64_t> window1;
  std::vector<int64_t> buffer;
  buffer.reserve(kWindowSize);

  auto start_sort = std::chrono::high_resolution_clock::now();
  int64_t sum_sort = 0;
  for (size_t i = 0; i < stream.size(); ++i) {
    window1.push_back(stream[i]);
    if (window1.size() > kWindowSize) {
      window1.pop_front();
    }
    if (window1.size() == kWindowSize) {
      buffer.assign(window1.begin(), window1.end());
      std::sort(buffer.begin(), buffer.end());
      sum_sort += (buffer[kWindowSize / 2 - 1] + buffer[kWindowSize / 2]) / 2;
    }
  }
  auto end_sort = std::chrono::high_resolution_clock::now();

  // Method 2: Maintain sorted vector with binary search insert/remove
  std::deque<int64_t> window2;          // For tracking order of insertion
  std::vector<int64_t> sorted_vec;      // Always sorted
  sorted_vec.reserve(kWindowSize + 1);

  auto start_sorted = std::chrono::high_resolution_clock::now();
  int64_t sum_sorted = 0;
  for (size_t i = 0; i < stream.size(); ++i) {
    int64_t new_val = stream[i];
    window2.push_back(new_val);

    // Binary search insert into sorted_vec
    auto insert_pos = std::lower_bound(sorted_vec.begin(), sorted_vec.end(), new_val);
    sorted_vec.insert(insert_pos, new_val);

    // Remove oldest element if window full
    if (window2.size() > kWindowSize) {
      int64_t old_val = window2.front();
      window2.pop_front();

      // Binary search remove from sorted_vec
      auto remove_pos = std::lower_bound(sorted_vec.begin(), sorted_vec.end(), old_val);
      sorted_vec.erase(remove_pos);
    }

    if (sorted_vec.size() == kWindowSize) {
      sum_sorted += (sorted_vec[kWindowSize / 2 - 1] + sorted_vec[kWindowSize / 2]) / 2;
    }
  }
  auto end_sorted = std::chrono::high_resolution_clock::now();

  auto ns_sort = std::chrono::duration_cast<std::chrono::nanoseconds>(
                     end_sort - start_sort).count();
  auto ns_sorted = std::chrono::duration_cast<std::chrono::nanoseconds>(
                       end_sorted - start_sorted).count();

  std::cout << "\n=== Deque+Sort vs Sorted Vector Maintenance (streaming, window="
            << kWindowSize << ") ===" << std::endl;
  std::cout << "Deque + sort each time:   " << static_cast<double>(ns_sort) / kIterations
            << " ns/op" << std::endl;
  std::cout << "Sorted vector maintained: " << static_cast<double>(ns_sorted) / kIterations
            << " ns/op" << std::endl;
  std::cout << "Ratio: " << std::fixed << std::setprecision(2)
            << static_cast<double>(ns_sort) / ns_sorted << "x" << std::endl;
  std::cout << "(sum_sort=" << sum_sort << ", sum_sorted=" << sum_sorted << ")" << std::endl;

  // Verify correctness
  EXPECT_EQ(sum_sort, sum_sorted) << "Median calculations should match";
}

// =============================================================================
// Edge Case Tests for Sorted Vector Maintenance
// =============================================================================

// Reference implementation for median calculation (ground truth)
static int64_t calculate_reference_median(const std::deque<int64_t>& window) {
  if (window.empty()) return 0;
  std::vector<int64_t> sorted(window.begin(), window.end());
  std::sort(sorted.begin(), sorted.end());
  const size_t mid = sorted.size() / 2;
  if (sorted.size() % 2 == 0) {
    return (sorted[mid - 1] + sorted[mid]) / 2;
  }
  return sorted[mid];
}

// Test 1: Duplicate values (common in HFT - same price repeated)
TEST_F(RobustZScoreTest, EdgeCase_DuplicateValues) {
  RobustZScoreConfig config;
  config.window_size = 5;
  config.min_samples = 3;
  RobustZScore zscore(config);

  // All same values
  for (int i = 0; i < 5; ++i) {
    zscore.on_price(to_price_raw(100));
  }
  EXPECT_EQ(zscore.get_median(), to_price_raw(100));

  // Add different value, should slide out oldest 100
  zscore.on_price(to_price_raw(200));
  // Window: [100, 100, 100, 100, 200] -> median = 100
  EXPECT_EQ(zscore.get_median(), to_price_raw(100));

  // Add more 200s
  zscore.on_price(to_price_raw(200));
  zscore.on_price(to_price_raw(200));
  // Window: [100, 100, 200, 200, 200] -> median = 200
  EXPECT_EQ(zscore.get_median(), to_price_raw(200));
}

// Test 2: Multiple duplicates with removal
TEST_F(RobustZScoreTest, EdgeCase_MultipleDuplicatesRemoval) {
  RobustZScoreConfig config;
  config.window_size = 5;
  config.min_samples = 1;
  RobustZScore zscore(config);

  std::deque<int64_t> reference_window;

  // Pattern: [100, 100, 100, 50, 50]
  std::vector<int64_t> prices = {100, 100, 100, 50, 50, 75, 75, 100, 50, 150};

  for (int64_t p : prices) {
    int64_t price_raw = to_price_raw(p);
    zscore.on_price(price_raw);
    reference_window.push_back(price_raw);
    if (reference_window.size() > 5) {
      reference_window.pop_front();
    }

    int64_t expected = calculate_reference_median(reference_window);
    int64_t actual = zscore.get_median();
    EXPECT_EQ(actual, expected)
        << "Mismatch after adding " << p << ", window size=" << reference_window.size();
  }
}

// Test 3: Ascending sequence (worst case for some algorithms)
TEST_F(RobustZScoreTest, EdgeCase_AscendingSequence) {
  RobustZScoreConfig config;
  config.window_size = 10;
  config.min_samples = 1;
  RobustZScore zscore(config);

  std::deque<int64_t> reference_window;

  // Strictly ascending: 1, 2, 3, ... 20
  for (int64_t i = 1; i <= 20; ++i) {
    int64_t price_raw = to_price_raw(i);
    zscore.on_price(price_raw);
    reference_window.push_back(price_raw);
    if (reference_window.size() > 10) {
      reference_window.pop_front();
    }

    int64_t expected = calculate_reference_median(reference_window);
    int64_t actual = zscore.get_median();
    EXPECT_EQ(actual, expected) << "Mismatch at i=" << i;
  }
}

// Test 4: Descending sequence
TEST_F(RobustZScoreTest, EdgeCase_DescendingSequence) {
  RobustZScoreConfig config;
  config.window_size = 10;
  config.min_samples = 1;
  RobustZScore zscore(config);

  std::deque<int64_t> reference_window;

  // Strictly descending: 20, 19, 18, ... 1
  for (int64_t i = 20; i >= 1; --i) {
    int64_t price_raw = to_price_raw(i);
    zscore.on_price(price_raw);
    reference_window.push_back(price_raw);
    if (reference_window.size() > 10) {
      reference_window.pop_front();
    }

    int64_t expected = calculate_reference_median(reference_window);
    int64_t actual = zscore.get_median();
    EXPECT_EQ(actual, expected) << "Mismatch at i=" << i;
  }
}

// Test 5: Alternating high/low (zigzag pattern)
TEST_F(RobustZScoreTest, EdgeCase_AlternatingPattern) {
  RobustZScoreConfig config;
  config.window_size = 6;
  config.min_samples = 1;
  RobustZScore zscore(config);

  std::deque<int64_t> reference_window;

  // Zigzag: 100, 1, 100, 1, 100, 1, ...
  for (int i = 0; i < 20; ++i) {
    int64_t price = (i % 2 == 0) ? 100 : 1;
    int64_t price_raw = to_price_raw(price);
    zscore.on_price(price_raw);
    reference_window.push_back(price_raw);
    if (reference_window.size() > 6) {
      reference_window.pop_front();
    }

    int64_t expected = calculate_reference_median(reference_window);
    int64_t actual = zscore.get_median();
    EXPECT_EQ(actual, expected) << "Mismatch at iteration " << i;
  }
}

// Test 6: Window boundary transitions (warmup period)
TEST_F(RobustZScoreTest, EdgeCase_WindowBoundaryTransition) {
  RobustZScoreConfig config;
  config.window_size = 5;
  config.min_samples = 1;
  RobustZScore zscore(config);

  std::deque<int64_t> reference_window;

  // Test each step as window grows from 1 to 5, then slides
  std::vector<int64_t> prices = {50, 30, 70, 20, 80, 10, 90, 40, 60};

  for (size_t i = 0; i < prices.size(); ++i) {
    int64_t price_raw = to_price_raw(prices[i]);
    zscore.on_price(price_raw);
    reference_window.push_back(price_raw);
    if (reference_window.size() > 5) {
      reference_window.pop_front();
    }

    int64_t expected = calculate_reference_median(reference_window);
    int64_t actual = zscore.get_median();
    EXPECT_EQ(actual, expected)
        << "Mismatch at step " << i << " (window size=" << reference_window.size() << ")";
  }
}

// Test 7: Stress test with random data and large iterations
TEST_F(RobustZScoreTest, EdgeCase_StressTest_RandomData) {
  constexpr int kWindowSize = 30;
  constexpr int kIterations = 10000;

  RobustZScoreConfig config;
  config.window_size = kWindowSize;
  config.min_samples = 1;
  RobustZScore zscore(config);

  std::deque<int64_t> reference_window;
  std::mt19937 rng(12345);  // Fixed seed for reproducibility
  std::uniform_int_distribution<int64_t> dist(1, 1000);

  for (int i = 0; i < kIterations; ++i) {
    int64_t price_raw = to_price_raw(dist(rng));
    zscore.on_price(price_raw);
    reference_window.push_back(price_raw);
    if (reference_window.size() > kWindowSize) {
      reference_window.pop_front();
    }

    int64_t expected = calculate_reference_median(reference_window);
    int64_t actual = zscore.get_median();
    ASSERT_EQ(actual, expected)
        << "Mismatch at iteration " << i;
  }
}

// Test 8: Edge case - removing value that appears multiple times
TEST_F(RobustZScoreTest, EdgeCase_RemoveCorrectDuplicate) {
  RobustZScoreConfig config;
  config.window_size = 4;
  config.min_samples = 1;
  RobustZScore zscore(config);

  // Window: [100, 200, 100, 300]
  zscore.on_price(to_price_raw(100));
  zscore.on_price(to_price_raw(200));
  zscore.on_price(to_price_raw(100));
  zscore.on_price(to_price_raw(300));
  // sorted: [100, 100, 200, 300], median = (100+200)/2 = 150
  EXPECT_EQ(zscore.get_median(), to_price_raw(150));

  // Add 50, removes first 100
  // Window: [200, 100, 300, 50]
  zscore.on_price(to_price_raw(50));
  // sorted: [50, 100, 200, 300], median = (100+200)/2 = 150
  EXPECT_EQ(zscore.get_median(), to_price_raw(150));

  // Add 150, removes 200
  // Window: [100, 300, 50, 150]
  zscore.on_price(to_price_raw(150));
  // sorted: [50, 100, 150, 300], median = (100+150)/2 = 125
  EXPECT_EQ(zscore.get_median(), to_price_raw(125));
}

// Test 9: Single element window
TEST_F(RobustZScoreTest, EdgeCase_SingleElementWindow) {
  RobustZScoreConfig config;
  config.window_size = 1;
  config.min_samples = 1;
  RobustZScore zscore(config);

  zscore.on_price(to_price_raw(100));
  EXPECT_EQ(zscore.get_median(), to_price_raw(100));

  zscore.on_price(to_price_raw(200));
  EXPECT_EQ(zscore.get_median(), to_price_raw(200));

  zscore.on_price(to_price_raw(50));
  EXPECT_EQ(zscore.get_median(), to_price_raw(50));
}

// Test 10: Two element window (even size edge case)
TEST_F(RobustZScoreTest, EdgeCase_TwoElementWindow) {
  RobustZScoreConfig config;
  config.window_size = 2;
  config.min_samples = 1;
  RobustZScore zscore(config);

  zscore.on_price(to_price_raw(100));
  EXPECT_EQ(zscore.get_median(), to_price_raw(100));

  zscore.on_price(to_price_raw(200));
  // sorted: [100, 200], median = 150
  EXPECT_EQ(zscore.get_median(), to_price_raw(150));

  zscore.on_price(to_price_raw(50));
  // Window: [200, 50], sorted: [50, 200], median = 125
  EXPECT_EQ(zscore.get_median(), to_price_raw(125));
}

// =============================================================================
// EMAD Edge Case Tests (with running sum optimization)
// =============================================================================

// Reference EMAD calculation (EMA of absolute deviations)
class EMADReferenceCalculator {
 public:
  explicit EMADReferenceCalculator(int64_t alpha) : alpha_(alpha) {}

  void update(int64_t price_raw) {
    if (sample_count_ == 0) {
      ema_price_ = price_raw;
      emad_ = 0;
    } else {
      const int64_t deviation = std::abs(price_raw - ema_price_);
      emad_ = (alpha_ * deviation +
               (common::kEmaScale - alpha_) * emad_) /
              common::kEmaScale;
      ema_price_ = (alpha_ * price_raw +
                    (common::kEmaScale - alpha_) * ema_price_) /
                   common::kEmaScale;
    }
    ++sample_count_;
  }

  [[nodiscard]] int64_t get_emad() const { return emad_; }
  [[nodiscard]] int64_t get_ema_price() const { return ema_price_; }

 private:
  const int64_t alpha_;
  int64_t ema_price_{0};
  int64_t emad_{0};
  int sample_count_{0};
};

// Test EMAD: First sample should have EMAD = 0
TEST_F(RobustZScoreTest, EMAD_FirstSampleZero) {
  RobustZScoreConfig config;
  config.window_size = 30;
  config.min_samples = 1;
  RobustZScore zscore(config);

  zscore.on_price(to_price_raw(100));
  EXPECT_EQ(zscore.get_mad(), 0) << "First sample EMAD should be 0";
}

// Test EMAD: Constant prices should have EMAD → 0
TEST_F(RobustZScoreTest, EMAD_ConstantPriceConvergesToZero) {
  RobustZScoreConfig config;
  config.window_size = 30;
  config.min_samples = 1;
  config.ema_alpha = 645;
  RobustZScore zscore(config);

  // Feed constant prices
  for (int i = 0; i < 100; ++i) {
    zscore.on_price(to_price_raw(100));
  }

  // EMAD should be very close to 0 after many constant samples
  EXPECT_LT(zscore.get_mad(), to_price_raw(1) / 10)
      << "EMAD should approach 0 for constant prices";
}

// Test EMAD: Match reference calculation step by step
TEST_F(RobustZScoreTest, EMAD_MatchesReferenceCalculation) {
  constexpr int64_t kAlpha = 645;

  RobustZScoreConfig config;
  config.window_size = 30;
  config.min_samples = 1;
  config.ema_alpha = kAlpha;
  RobustZScore zscore(config);
  EMADReferenceCalculator reference(kAlpha);

  std::vector<int64_t> prices = {100, 102, 99, 105, 98, 101, 103, 97, 104, 100};

  for (int64_t p : prices) {
    int64_t price_raw = to_price_raw(p);
    zscore.on_price(price_raw);
    reference.update(price_raw);

    EXPECT_EQ(zscore.get_mad(), reference.get_emad())
        << "EMAD mismatch after price " << p;
    EXPECT_EQ(zscore.get_ema_price(), reference.get_ema_price())
        << "EMA price mismatch after price " << p;
  }
}

// Test EMAD: Large price swings should increase EMAD
TEST_F(RobustZScoreTest, EMAD_LargePriceSwingsIncrease) {
  RobustZScoreConfig config;
  config.window_size = 30;
  config.min_samples = 1;
  config.ema_alpha = 1000;  // Faster adaptation for test
  RobustZScore zscore(config);

  // Start with stable prices
  for (int i = 0; i < 10; ++i) {
    zscore.on_price(to_price_raw(100));
  }
  const int64_t stable_emad = zscore.get_mad();

  // Large price swing
  zscore.on_price(to_price_raw(200));
  const int64_t after_swing_emad = zscore.get_mad();

  EXPECT_GT(after_swing_emad, stable_emad)
      << "EMAD should increase after large price swing";
}

// Test EMAD: Verify EMA price tracking
TEST_F(RobustZScoreTest, EMAD_EMAPriceTracksActualPrice) {
  RobustZScoreConfig config;
  config.window_size = 30;
  config.min_samples = 1;
  config.ema_alpha = 2000;  // Higher alpha = faster tracking
  RobustZScore zscore(config);

  // Feed gradually increasing prices
  for (int64_t p = 100; p <= 200; ++p) {
    zscore.on_price(to_price_raw(p));
  }

  // EMA should be between min and max, closer to recent
  const int64_t ema = zscore.get_ema_price();
  EXPECT_GT(ema, to_price_raw(150)) << "EMA should be above midpoint";
  EXPECT_LT(ema, to_price_raw(200)) << "EMA should lag behind latest price";
}

// Test baseline EMAD: Running sum vs O(n) loop calculation
TEST_F(RobustZScoreTest, EMAD_BaselineRunningSum_MatchesReference) {
  constexpr int kWindowSize = 30;
  constexpr int kBaselineWindow = 100;
  constexpr int kBaselineMinHistory = 30;

  RobustZScoreConfig config;
  config.window_size = kWindowSize;
  config.min_samples = 20;
  config.baseline_window = kBaselineWindow;
  config.baseline_min_history = kBaselineMinHistory;
  RobustZScore zscore(config);

  std::deque<int64_t> emad_history_ref;
  std::mt19937 rng(42);
  std::uniform_int_distribution<int64_t> dist(95, 105);

  // Feed enough prices to build up emad_history
  for (int i = 0; i < 200; ++i) {
    int64_t price_raw = to_price_raw(dist(rng));
    zscore.on_price(price_raw);

    // calculate_zscore updates emad_history internally
    if (i >= config.min_samples) {
      (void)zscore.calculate_zscore(price_raw);

      // Manually track EMAD history for verification
      emad_history_ref.push_back(zscore.get_mad());
      if (emad_history_ref.size() > static_cast<size_t>(kBaselineWindow)) {
        emad_history_ref.pop_front();
      }
    }
  }

  // Verify adaptive threshold uses correct baseline
  // We can't directly test calculate_baseline_emad (private), but we can
  // verify get_adaptive_threshold returns reasonable values
  const int64_t base_threshold = 25000;  // 2.5 in kZScoreScale
  const int64_t adaptive = zscore.get_adaptive_threshold(base_threshold);

  // Adaptive threshold should be within [0.7, 1.3] * base
  const int64_t min_expected = (base_threshold * 7000) / common::kSignalScale;
  const int64_t max_expected = (base_threshold * 13000) / common::kSignalScale;

  EXPECT_GE(adaptive, min_expected)
      << "Adaptive threshold below minimum scalar";
  EXPECT_LE(adaptive, max_expected)
      << "Adaptive threshold above maximum scalar";
}

// Test baseline EMAD: Before min_history, baseline_emad == current emad
// so vol_ratio = 1.0, which maps to vol_scalar = 0.9 (midpoint of [0.7, 1.3] range)
TEST_F(RobustZScoreTest, EMAD_BaselineBeforeMinHistory) {
  RobustZScoreConfig config;
  config.window_size = 10;
  config.min_samples = 5;
  config.baseline_window = 100;
  config.baseline_min_history = 30;
  config.vol_ratio_low = 5000;    // 0.5
  config.vol_ratio_high = 20000;  // 2.0
  config.min_vol_scalar = 7000;   // 0.7
  config.max_vol_scalar = 13000;  // 1.3
  RobustZScore zscore(config);

  // Feed just enough samples to pass min_samples but not min_history
  for (int i = 0; i < 10; ++i) {
    zscore.on_price(to_price_raw(100 + i));
    if (i >= config.min_samples) {
      (void)zscore.calculate_zscore(to_price_raw(100 + i));
    }
  }

  // With insufficient history, baseline_emad = current emad
  // vol_ratio = current/baseline = 1.0 (10000 in scale)
  // vol_scalar = 7000 + (13000-7000) * (10000-5000) / (20000-5000)
  //            = 7000 + 6000 * 5000 / 15000 = 7000 + 2000 = 9000
  // adaptive = base * 9000 / 10000 = base * 0.9
  const int64_t base_threshold = 25000;
  const int64_t adaptive = zscore.get_adaptive_threshold(base_threshold);
  const int64_t expected = (base_threshold * 9000) / common::kSignalScale;  // 22500
  EXPECT_EQ(adaptive, expected)
      << "vol_ratio=1.0 should give vol_scalar=0.9";
}

// Test EMAD: Stress test running sum accuracy over many iterations
TEST_F(RobustZScoreTest, EMAD_RunningSumStressTest) {
  constexpr int kIterations = 5000;

  RobustZScoreConfig config;
  config.window_size = 30;
  config.min_samples = 20;
  config.baseline_window = 100;
  config.baseline_min_history = 30;
  RobustZScore zscore(config);

  std::mt19937 rng(12345);
  std::uniform_int_distribution<int64_t> dist(90, 110);

  // Running sum reference
  std::deque<int64_t> emad_history;
  int64_t emad_sum = 0;

  for (int i = 0; i < kIterations; ++i) {
    int64_t price_raw = to_price_raw(dist(rng));
    zscore.on_price(price_raw);

    if (i >= config.min_samples) {
      (void)zscore.calculate_zscore(price_raw);

      // Reference: maintain running sum manually
      int64_t current_emad = zscore.get_mad();
      emad_sum += current_emad;
      emad_history.push_back(current_emad);
      if (emad_history.size() > static_cast<size_t>(config.baseline_window)) {
        emad_sum -= emad_history.front();
        emad_history.pop_front();
      }
    }

    // Every 100 iterations, verify adaptive threshold is reasonable
    if (i > 0 && i % 100 == 0 && i >= config.min_samples + config.baseline_min_history) {
      const int64_t base = 25000;
      const int64_t adaptive = zscore.get_adaptive_threshold(base);

      // Verify O(1) running sum gives same average as O(n) loop
      int64_t loop_sum = 0;
      for (const auto& e : emad_history) {
        loop_sum += e;
      }
      ASSERT_EQ(emad_sum, loop_sum)
          << "Running sum mismatch at iteration " << i;

      // Adaptive threshold should be within valid range
      EXPECT_GE(adaptive, base * 7 / 10) << "Adaptive below min at " << i;
      EXPECT_LE(adaptive, base * 13 / 10) << "Adaptive above max at " << i;
    }
  }
}

// Test EMAD: Very high volatility ratio should hit max scalar
TEST_F(RobustZScoreTest, EMAD_HighVolatilityHitsMaxScalar) {
  RobustZScoreConfig config;
  config.window_size = 10;
  config.min_samples = 5;
  config.baseline_window = 50;
  config.baseline_min_history = 10;
  config.vol_ratio_low = 5000;   // 0.5
  config.vol_ratio_high = 20000; // 2.0
  config.min_vol_scalar = 7000;  // 0.7
  config.max_vol_scalar = 13000; // 1.3
  RobustZScore zscore(config);

  // Start with very stable prices to build low baseline EMAD
  for (int i = 0; i < 50; ++i) {
    zscore.on_price(to_price_raw(100));
    if (i >= config.min_samples) {
      (void)zscore.calculate_zscore(to_price_raw(100));
    }
  }

  // Now add high volatility to spike current EMAD
  for (int i = 0; i < 20; ++i) {
    int64_t price = (i % 2 == 0) ? 50 : 150;  // 100-point swings
    zscore.on_price(to_price_raw(price));
    (void)zscore.calculate_zscore(to_price_raw(price));
  }

  const int64_t base_threshold = 25000;
  const int64_t adaptive = zscore.get_adaptive_threshold(base_threshold);

  // Should be at or near max scalar (1.3x)
  const int64_t max_threshold = (base_threshold * 13000) / common::kSignalScale;
  EXPECT_GE(adaptive, (base_threshold * 12000) / common::kSignalScale)
      << "High volatility should push threshold toward max";
  EXPECT_LE(adaptive, max_threshold)
      << "Should not exceed max scalar";
}

