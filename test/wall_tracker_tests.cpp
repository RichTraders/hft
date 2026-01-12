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

#include "strategy/mean_reversion_maker/wall_detector.h"

using namespace trading;
using namespace trading::wall_constants;

class WallTrackerTest : public ::testing::Test {
 protected:
  WallTracker tracker;

  void SetUp() override { tracker.reset(); }
};

// ========================================
// Basic Update Tests
// ========================================

TEST_F(WallTrackerTest, InitialState) {
  EXPECT_EQ(tracker.first_seen, 0u);
  EXPECT_EQ(tracker.last_update, 0u);
  EXPECT_EQ(tracker.snapshot_count, 0);
  EXPECT_EQ(tracker.buffer_size(), 0u);
}

TEST_F(WallTrackerTest, SingleUpdate) {
  const uint64_t now = 1'000'000'000;  // 1 second
  const int64_t notional = 100'000;
  const int64_t distance = 5;

  tracker.update(now, notional, distance);

  EXPECT_EQ(tracker.first_seen, now);
  EXPECT_EQ(tracker.last_update, now);
  EXPECT_EQ(tracker.snapshot_count, 1);
  EXPECT_EQ(tracker.buffer_size(), 1u);
}

TEST_F(WallTrackerTest, FirstUpdate_TimestampZero) {
  // Edge case: first update with timestamp 0 should still work
  tracker.update(0, 100'000, 5);

  EXPECT_EQ(tracker.first_seen, 0u);
  EXPECT_EQ(tracker.last_update, 0u);
  EXPECT_EQ(tracker.snapshot_count, 1);

  // Second update should NOT overwrite first_seen
  tracker.update(100'000'000, 100'000, 5);

  EXPECT_EQ(tracker.first_seen, 0u);  // Still 0 from first update
  EXPECT_EQ(tracker.last_update, 100'000'000u);
  EXPECT_EQ(tracker.snapshot_count, 2);
}

TEST_F(WallTrackerTest, MultipleUpdates_StartAtZero) {
  // Edge case: multiple updates starting from timestamp 0
  for (int i = 0; i < 10; ++i) {
    tracker.update(static_cast<uint64_t>(i) * 100'000'000, 100'000 + i * 1000,
        5 + i);
  }

  EXPECT_EQ(tracker.first_seen, 0u);  // First timestamp was 0
  EXPECT_EQ(tracker.last_update, 900'000'000u);
  EXPECT_EQ(tracker.snapshot_count, 10);
  EXPECT_EQ(tracker.buffer_size(), 10u);
}

TEST_F(WallTrackerTest, MultipleUpdates) {
  // Note: start at 1 to avoid first_seen == 0 issue
  const uint64_t start = 1'000'000'000;
  for (int i = 0; i < 10; ++i) {
    tracker.update(start + static_cast<uint64_t>(i) * 100'000'000,
        100'000 + i * 1000, 5 + i);
  }

  EXPECT_EQ(tracker.first_seen, start);
  EXPECT_EQ(tracker.last_update, start + 900'000'000u);
  EXPECT_EQ(tracker.snapshot_count, 10);
  EXPECT_EQ(tracker.buffer_size(), 10u);
}

TEST_F(WallTrackerTest, CircularBufferWrap) {
  // Fill buffer beyond kMaxSnapshots (20)
  // Note: start at 1 to avoid first_seen == 0 issue
  const uint64_t start = 1'000'000'000;
  for (size_t i = 0; i < kMaxSnapshots + 5; ++i) {
    tracker.update(start + i * 100'000'000,
        static_cast<int64_t>(100'000 + i * 1000), static_cast<int64_t>(5));
  }

  EXPECT_EQ(tracker.snapshot_count, static_cast<int>(kMaxSnapshots + 5));
  EXPECT_EQ(tracker.buffer_size(), kMaxSnapshots);  // Capped at max
}

TEST_F(WallTrackerTest, Reset) {
  // Add some data
  for (int i = 0; i < 5; ++i) {
    tracker.update(static_cast<uint64_t>(i) * 100'000'000, 100'000, 5);
  }

  tracker.reset();

  EXPECT_EQ(tracker.first_seen, 0u);
  EXPECT_EQ(tracker.last_update, 0u);
  EXPECT_EQ(tracker.snapshot_count, 0);
  EXPECT_EQ(tracker.buffer_size(), 0u);
}

// ========================================
// Persistence Score Tests
// ========================================

TEST_F(WallTrackerTest, PersistenceScore_NotEnoughSnapshots) {
  // Less than kMinSnapshotsForPersistence (5)
  for (int i = 0; i < kMinSnapshotsForPersistence - 1; ++i) {
    tracker.update(static_cast<uint64_t>(i) * 100'000'000, 100'000, 5);
  }

  EXPECT_EQ(tracker.persistence_score(), 0);
}

TEST_F(WallTrackerTest, PersistenceScore_ZeroDuration) {
  // All updates at same timestamp
  for (int i = 0; i < kMinSnapshotsForPersistence; ++i) {
    tracker.update(1'000'000'000, 100'000, 5);
  }

  EXPECT_EQ(tracker.persistence_score(), 0);
}

TEST_F(WallTrackerTest, PersistenceScore_OneSecond) {
  // Duration = 1 second = half of 2-second divisor = 5000 score
  const uint64_t start = 1'000'000'000;
  for (int i = 0; i < kMinSnapshotsForPersistence; ++i) {
    tracker.update(start + static_cast<uint64_t>(i) * 250'000'000, 100'000, 5);
  }
  // Duration = 1 second (4 intervals of 250ms)
  EXPECT_EQ(tracker.persistence_score(), 5000);
}

TEST_F(WallTrackerTest, PersistenceScore_TwoSeconds) {
  // Duration = 2 seconds = full score (10000)
  const uint64_t start = 1'000'000'000;
  for (int i = 0; i < kMinSnapshotsForPersistence; ++i) {
    tracker.update(start + static_cast<uint64_t>(i) * 500'000'000, 100'000, 5);
  }
  // Duration = 2 seconds
  EXPECT_EQ(tracker.persistence_score(), common::kSignalScale);
}

TEST_F(WallTrackerTest, PersistenceScore_Clamped) {
  // Duration > 2 seconds should still clamp to kSignalScale
  // Note: start must be > 0 because first_seen uses "if (first_seen == 0)" check
  const uint64_t start = 1'000'000'000;
  for (int i = 0; i < kMinSnapshotsForPersistence; ++i) {
    tracker.update(start + static_cast<uint64_t>(i) * 1'000'000'000, 100'000,
        5);
  }
  // Duration = 4 seconds, score clamped to 10000
  EXPECT_EQ(tracker.persistence_score(), common::kSignalScale);
}

// ========================================
// Stability Score Tests
// ========================================

TEST_F(WallTrackerTest, StabilityScore_NotEnoughSnapshots) {
  // Less than kMinSnapshotsForStability (10)
  for (int i = 0; i < kMinSnapshotsForStability - 1; ++i) {
    tracker.update(static_cast<uint64_t>(i) * 100'000'000, 100'000, 5);
  }

  EXPECT_EQ(tracker.stability_score(), 0);
}

TEST_F(WallTrackerTest, StabilityScore_PerfectStability) {
  // All same values = zero variance = max stability
  for (int i = 0; i < kMinSnapshotsForStability; ++i) {
    tracker.update(static_cast<uint64_t>(i) * 100'000'000, 100'000, 5);
  }

  EXPECT_EQ(tracker.stability_score(), common::kSignalScale);
}

TEST_F(WallTrackerTest, StabilityScore_HighVariance) {
  // Alternating high and low values = high variance = low stability
  for (int i = 0; i < kMinSnapshotsForStability; ++i) {
    const int64_t notional = (i % 2 == 0) ? 50'000 : 150'000;
    tracker.update(static_cast<uint64_t>(i) * 100'000'000, notional, 5);
  }

  // Should have low stability score due to high variance
  EXPECT_LT(tracker.stability_score(), common::kSignalScale / 2);
}

TEST_F(WallTrackerTest, StabilityScore_ZeroAverage) {
  // All zero values
  for (int i = 0; i < kMinSnapshotsForStability; ++i) {
    tracker.update(static_cast<uint64_t>(i) * 100'000'000, 0, 5);
  }

  EXPECT_EQ(tracker.stability_score(), 0);
}

// ========================================
// Distance Consistency Score Tests
// ========================================

TEST_F(WallTrackerTest, DistanceScore_NotEnoughSnapshots) {
  for (int i = 0; i < kMinSnapshotsForStability - 1; ++i) {
    tracker.update(static_cast<uint64_t>(i) * 100'000'000, 100'000, 5);
  }

  EXPECT_EQ(tracker.distance_consistency_score(), 0);
}

TEST_F(WallTrackerTest, DistanceScore_CloseDistance) {
  // Average distance <= 5 bps = max score
  for (int i = 0; i < kMinSnapshotsForStability; ++i) {
    tracker.update(static_cast<uint64_t>(i) * 100'000'000, 100'000,
        kDistanceGoodBps);
  }

  EXPECT_EQ(tracker.distance_consistency_score(), common::kSignalScale);
}

TEST_F(WallTrackerTest, DistanceScore_FarDistance) {
  // Average distance >= 15 bps = zero score
  for (int i = 0; i < kMinSnapshotsForStability; ++i) {
    tracker.update(static_cast<uint64_t>(i) * 100'000'000, 100'000,
        kDistanceBadBps);
  }

  EXPECT_EQ(tracker.distance_consistency_score(), 0);
}

TEST_F(WallTrackerTest, DistanceScore_MidDistance) {
  // Average distance = 10 bps (midpoint) = 5000 score
  for (int i = 0; i < kMinSnapshotsForStability; ++i) {
    tracker.update(static_cast<uint64_t>(i) * 100'000'000, 100'000, 10);
  }

  // score = 10000 * (15 - 10) / 10 = 5000
  EXPECT_EQ(tracker.distance_consistency_score(), 5000);
}

// ========================================
// Composite Quality Tests
// ========================================

TEST_F(WallTrackerTest, CompositeQuality_AllZero) {
  // Not enough snapshots for any score
  tracker.update(0, 100'000, 5);

  EXPECT_EQ(tracker.composite_quality(), 0);
}

TEST_F(WallTrackerTest, CompositeQuality_MaxScores) {
  // Setup for maximum scores:
  // - Enough snapshots (10)
  // - 2+ seconds duration (need >= 2s between first and last)
  // - Perfect stability (same values)
  // - Close distance (5 bps)
  // Note: start must be > 0 because first_seen uses "if (first_seen == 0)" check
  const uint64_t start = 1'000'000'000;  // 1 second offset
  for (int i = 0; i < kMinSnapshotsForStability; ++i) {
    // ~222ms intervals for 2+ second total duration (9 intervals * 222.2ms >= 2s)
    tracker.update(start + static_cast<uint64_t>(i) * 222'222'223, 100'000,
        kDistanceGoodBps);
  }

  // Duration = 9 * 222.2ms = 2.0s -> persistence = 10000
  // Stability = 10000 (all same values)
  // Distance = 10000 (5 bps)
  // Composite = (10000*5000 + 10000*3500 + 10000*1500) / 10000 = 10000
  EXPECT_EQ(tracker.composite_quality(), common::kSignalScale);
}

TEST_F(WallTrackerTest, CompositeQuality_WeightedAverage) {
  // Create scenario with different scores for each component
  // This is a sanity check that weights are applied correctly
  // Note: start must be > 0 because first_seen uses "if (first_seen == 0)" check
  const uint64_t start = 1'000'000'000;

  // 10 snapshots, 1.8 second duration
  // Same values (stability = 10000)
  // Distance = 10 bps (distance = 5000)
  for (int i = 0; i < kMinSnapshotsForStability; ++i) {
    tracker.update(start + static_cast<uint64_t>(i) * 200'000'000, 100'000, 10);
  }

  const int64_t persistence = tracker.persistence_score();
  const int64_t stability = tracker.stability_score();
  const int64_t distance = tracker.distance_consistency_score();

  // Verify individual scores
  EXPECT_EQ(persistence, 9000);  // 1.8s / 2s * 10000
  EXPECT_EQ(stability, common::kSignalScale);  // All same values
  EXPECT_EQ(distance, 5000);  // 10 bps (midpoint)

  // Manual calculation
  const int64_t expected =
      (stability * kStabilityWeight + persistence * kPersistenceWeight +
          distance * kDistanceWeight) /
      common::kSignalScale;

  // (10000*5000 + 9000*3500 + 5000*1500) / 10000 = 8900
  EXPECT_EQ(expected, 8900);
  EXPECT_EQ(tracker.composite_quality(), expected);
}

// ========================================
// Circular Buffer Correctness Tests
// ========================================

TEST_F(WallTrackerTest, CircularBuffer_CorrectValues) {
  // Fill with sequential values, then wrap and verify calculations are correct
  const size_t total = kMaxSnapshots + 5;

  for (size_t i = 0; i < total; ++i) {
    // Notional increases: 100000, 101000, 102000, ...
    tracker.update(i * 100'000'000, static_cast<int64_t>(100'000 + i * 1000),
        5);
  }

  // Buffer should contain values from index 5 to 24 (the last 20 values)
  // Average should be (100000 + 5*1000) to (100000 + 24*1000)
  // = 105000 to 124000, average = 114500
  const int64_t expected_avg =
      (105000 + 124000) / 2;  // Approximate midpoint of range

  // Stability score with sequential increasing values
  // Variance will be based on difference from mean
  EXPECT_GT(tracker.stability_score(), 0);

  // Distance should still work correctly
  EXPECT_EQ(tracker.distance_consistency_score(), common::kSignalScale);
}

TEST_F(WallTrackerTest, CircularBuffer_AfterReset) {
  // Fill, reset, fill again - verify clean state
  for (size_t i = 0; i < kMaxSnapshots; ++i) {
    tracker.update(i * 100'000'000, 100'000, 10);
  }

  tracker.reset();

  // Fill with different values
  for (int i = 0; i < kMinSnapshotsForStability; ++i) {
    tracker.update(static_cast<uint64_t>(i) * 100'000'000, 200'000, 5);
  }

  // Should reflect new values only
  EXPECT_EQ(tracker.distance_consistency_score(), common::kSignalScale);
}
