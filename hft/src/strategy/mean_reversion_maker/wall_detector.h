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

#ifndef WALL_DETECTOR_H
#define WALL_DETECTOR_H

#include <algorithm>
#include <cstdint>
#include <deque>
#include <numeric>
#include <span>
#include <vector>

#include "common/fixed_point_config.hpp"
#include "common/types.h"

namespace trading {

// === Wall detector constants ===
namespace wall_constants {
inline constexpr size_t kMaxSnapshots = 20;
inline constexpr int kMinSnapshotsForPersistence = 5;
inline constexpr int kMinSnapshotsForStability = 10;
inline constexpr int64_t kPersistenceNsDivisor =
    2'000'000'000;  // 2 seconds in ns
inline constexpr int64_t kDistanceGoodBps = 5;
inline constexpr int64_t kDistanceBadBps = 15;
inline constexpr int64_t kDistanceRange = 10;  // 15 - 5
inline constexpr int64_t kStabilityWeight = 5000;
inline constexpr int64_t kPersistenceWeight = 3500;
inline constexpr int64_t kDistanceWeight = 1500;
}  // namespace wall_constants

// Wall detection result structure
struct WallInfo {
  int64_t accumulated_notional{0};  // price * qty in raw scale
  int64_t distance_bps{0};          // Distance in basis points (15 = 0.15%)
  int levels_checked{0};
  bool is_valid{false};
};

// Wall quality tracking structure
struct WallTracker {
  uint64_t first_seen{0};
  uint64_t last_update{0};
  int snapshot_count{0};
  std::deque<int64_t> size_snapshots;      // Notional in raw scale
  std::deque<int64_t> distance_snapshots;  // Distance in bps

  void update(uint64_t now, int64_t notional_raw, int64_t distance_bps) {
    if (first_seen == 0) {
      first_seen = now;
    }
    last_update = now;
    snapshot_count++;

    size_snapshots.push_back(notional_raw);
    distance_snapshots.push_back(distance_bps);

    if (size_snapshots.size() > wall_constants::kMaxSnapshots) {
      size_snapshots.pop_front();
      distance_snapshots.pop_front();
    }
  }

  void reset() {
    first_seen = 0;
    last_update = 0;
    snapshot_count = 0;
    size_snapshots.clear();
    distance_snapshots.clear();
  }

  // Persistence score: How long has wall been present?
  // Returns [0, kSignalScale] where kSignalScale = 10000
  // 2+ seconds = 10000, 1 second = 5000, 0.5 seconds = 0
  [[nodiscard]] int64_t persistence_score() const {
    if (snapshot_count < wall_constants::kMinSnapshotsForPersistence) {
      return 0;
    }
    // duration in nanoseconds / 2e9 * kSignalScale
    // = (duration * kSignalScale) / 2e9
    const auto duration_ns = static_cast<int64_t>(last_update - first_seen);
    const int64_t score = (duration_ns * common::kSignalScale) /
                          wall_constants::kPersistenceNsDivisor;
    return std::clamp(score, int64_t{0}, common::kSignalScale);
  }

  // Stability score: Based on variance (no sqrt)
  // Low variance = high stability
  // Returns [0, kSignalScale]
  [[nodiscard]] int64_t stability_score() const {
    if (size_snapshots.size() <
        static_cast<size_t>(wall_constants::kMinSnapshotsForStability)) {
      return 0;
    }

    // Calculate average
    const int64_t sum = std::accumulate(size_snapshots.begin(),
        size_snapshots.end(),
        int64_t{0});
    const int64_t avg = sum / static_cast<int64_t>(size_snapshots.size());

    if (avg == 0)
      return 0;

    // Calculate variance (sum of squared deviations)
    int64_t variance_sum = 0;
    for (const int64_t snapshot_size : size_snapshots) {
      const int64_t diff = snapshot_size - avg;
      // Use __int128 to avoid overflow in squaring
      const auto squared = static_cast<__int128_t>(diff) * diff;
      variance_sum += static_cast<int64_t>(
          squared / avg);  // Normalize by avg to keep in range
    }
    const int64_t normalized_variance =
        variance_sum / static_cast<int64_t>(size_snapshots.size());

    // CV^2 threshold: if cv < 0.5, cv^2 < 0.25
    // normalized_variance / avg < 0.25 means stable
    // score = kSignalScale * (1 - normalized_variance / (avg * 0.25))
    // = kSignalScale * (1 - 4 * normalized_variance / avg)
    const int64_t threshold = avg / 4;  // 0.25 * avg
    if (threshold == 0)
      return common::kSignalScale;

    const int64_t score =
        common::kSignalScale -
        (normalized_variance * common::kSignalScale) / threshold;
    return std::clamp(score, int64_t{0}, common::kSignalScale);
  }

  // Distance consistency score
  // Close to BBO = good, far = bad
  // Returns [0, kSignalScale]
  [[nodiscard]] int64_t distance_consistency_score() const {
    if (distance_snapshots.size() <
        static_cast<size_t>(wall_constants::kMinSnapshotsForStability)) {
      return 0;
    }

    const int64_t sum = std::accumulate(distance_snapshots.begin(),
        distance_snapshots.end(),
        int64_t{0});
    const int64_t avg_bps =
        sum / static_cast<int64_t>(distance_snapshots.size());

    // Close to BBO = good (< 5 bps = 10000)
    // Far from BBO = bad (> 15 bps = 0)
    // Linear interpolation: score = kSignalScale * (15 - avg) / 10
    if (avg_bps <= wall_constants::kDistanceGoodBps)
      return common::kSignalScale;
    if (avg_bps >= wall_constants::kDistanceBadBps)
      return 0;

    return common::kSignalScale * (wall_constants::kDistanceBadBps - avg_bps) /
           wall_constants::kDistanceRange;
  }

  // Composite quality score (weighted average)
  // Returns [0, kSignalScale]
  [[nodiscard]] int64_t composite_quality() const {
    // Weights: stability 50%, persistence 35%, distance 15%
    return (stability_score() * wall_constants::kStabilityWeight +
               persistence_score() * wall_constants::kPersistenceWeight +
               distance_consistency_score() * wall_constants::kDistanceWeight) /
           common::kSignalScale;
  }
};

// Wall detection function
// Detects wall (large liquidity concentration) in orderbook
template <typename OrderBook>
[[nodiscard]] inline WallInfo detect_wall(const OrderBook* order_book,
    common::Side side, int max_levels, int64_t threshold_notional_raw,
    int64_t max_distance_bps, int min_price_int,
    std::vector<int64_t>& level_qty_buffer,
    std::vector<int>& level_idx_buffer) noexcept {
  WallInfo info;
  const auto* bbo = order_book->get_bbo();

  if (UNLIKELY(!bbo || bbo->bid_price == common::kPriceInvalid ||
               bbo->ask_price == common::kPriceInvalid)) {
    return info;
  }

  const int64_t base_price = (side == common::Side::kBuy)
                                 ? bbo->bid_price.value
                                 : bbo->ask_price.value;

  if (base_price == 0) {
    return info;
  }

  // Peek orderbook levels
  const int actual_levels = order_book->peek_qty(side == common::Side::kBuy,
      max_levels,
      std::span<int64_t>(level_qty_buffer),
      std::span<int>(level_idx_buffer));

  // For weighted average price calculation
  // Using __int128 to avoid overflow
  __int128_t weighted_sum = 0;

  for (int i = 0; i < actual_levels; ++i) {
    if (level_qty_buffer[i] <= 0)
      break;

    const int64_t price_idx = level_idx_buffer[i];
    const int64_t price_raw = min_price_int + price_idx;

    // notional = price * qty / kQtyScale (to normalize)
    // But we keep in raw scale for comparison
    const int64_t notional =
        (price_raw * level_qty_buffer[i]) / common::FixedPointConfig::kQtyScale;
    info.accumulated_notional += notional;
    weighted_sum += static_cast<__int128_t>(price_raw) * notional;
    info.levels_checked = i + 1;

    // Target amount reached
    if (info.accumulated_notional >= threshold_notional_raw) {
      // weighted_avg_price = weighted_sum / accumulated
      const auto weighted_avg_price =
          static_cast<int64_t>(weighted_sum / info.accumulated_notional);

      // distance_bps = |avg_price - base_price| * 10000 / base_price
      const int64_t delta = std::abs(weighted_avg_price - base_price);
      info.distance_bps = (delta * common::kBpsScale) / base_price;
      info.is_valid = (info.distance_bps <= max_distance_bps);

      break;
    }
  }

  if (info.accumulated_notional < threshold_notional_raw) {
    info.is_valid = false;
  }

  return info;
}

}  // namespace trading

#endif  // WALL_DETECTOR_H
