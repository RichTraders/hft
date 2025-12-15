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
#include "depth_validator.h"

using namespace trading;

// ============================================================================
// First Depth After Snapshot Tests - Spot
// ============================================================================

class SpotFirstDepthAfterSnapshotTest : public ::testing::Test {
 protected:
  static constexpr MarketType kMarket = MarketType::kSpot;
};

TEST_F(SpotFirstDepthAfterSnapshotTest, ValidWhenOverlapsSnapshot) {
  // Snapshot: 100, Depth: U=90, u=110
  // 90 <= 100 AND 110 >= 100 -> overlaps, valid
  auto result = validate_first_depth_after_snapshot<kMarket>(90, 110, 100);
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(result.new_update_index, 110);
}

TEST_F(SpotFirstDepthAfterSnapshotTest, ValidWhenImmediatelyFollows) {
  // Snapshot: 100, Depth: U=101, u=110
  // U == lastUpdateId + 1 -> immediately follows, valid
  auto result = validate_first_depth_after_snapshot<kMarket>(101, 110, 100);
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(result.new_update_index, 110);
}

TEST_F(SpotFirstDepthAfterSnapshotTest, InvalidWhenTooOld_EndLessOrEqualSnapshot) {
  // Spot: u <= lastUpdateId should be discarded
  // Snapshot: 100, Depth: U=80, u=100
  auto result = validate_first_depth_after_snapshot<kMarket>(80, 100, 100);
  EXPECT_FALSE(result.valid);
  EXPECT_EQ(result.new_update_index, 100);
}

TEST_F(SpotFirstDepthAfterSnapshotTest, InvalidWhenTooOld_EndBeforeSnapshot) {
  // Snapshot: 100, Depth: U=80, u=90
  auto result = validate_first_depth_after_snapshot<kMarket>(80, 90, 100);
  EXPECT_FALSE(result.valid);
  EXPECT_EQ(result.new_update_index, 100);
}

TEST_F(SpotFirstDepthAfterSnapshotTest, InvalidWhenGapExists) {
  // Snapshot: 100, Depth: U=105, u=110
  // Neither overlaps (105 > 100) nor immediately follows (105 != 101)
  auto result = validate_first_depth_after_snapshot<kMarket>(105, 110, 100);
  EXPECT_FALSE(result.valid);
  EXPECT_EQ(result.new_update_index, 100);
}

// ============================================================================
// First Depth After Snapshot Tests - Futures
// ============================================================================

class FuturesFirstDepthAfterSnapshotTest : public ::testing::Test {
 protected:
  static constexpr MarketType kMarket = MarketType::kFutures;
};

TEST_F(FuturesFirstDepthAfterSnapshotTest, ValidWhenOverlapsSnapshot) {
  // Snapshot: 100, Depth: U=90, u=110
  auto result = validate_first_depth_after_snapshot<kMarket>(90, 110, 100);
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(result.new_update_index, 110);
}

TEST_F(FuturesFirstDepthAfterSnapshotTest, ValidWhenImmediatelyFollows) {
  // Snapshot: 100, Depth: U=101, u=110
  auto result = validate_first_depth_after_snapshot<kMarket>(101, 110, 100);
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(result.new_update_index, 110);
}

TEST_F(FuturesFirstDepthAfterSnapshotTest, InvalidWhenTooOld_EndBeforeSnapshot) {
  // Futures: u < lastUpdateId should be discarded
  // Snapshot: 100, Depth: U=80, u=99
  auto result = validate_first_depth_after_snapshot<kMarket>(80, 99, 100);
  EXPECT_FALSE(result.valid);
  EXPECT_EQ(result.new_update_index, 100);
}

TEST_F(FuturesFirstDepthAfterSnapshotTest, ValidWhenEndEqualsSnapshot) {
  // Futures: u == lastUpdateId is valid (not discarded like Spot)
  // Snapshot: 100, Depth: U=90, u=100
  auto result = validate_first_depth_after_snapshot<kMarket>(90, 100, 100);
  EXPECT_TRUE(result.valid);  // overlaps
  EXPECT_EQ(result.new_update_index, 100);
}

TEST_F(FuturesFirstDepthAfterSnapshotTest, InvalidWhenGapExists) {
  // Snapshot: 100, Depth: U=105, u=110
  auto result = validate_first_depth_after_snapshot<kMarket>(105, 110, 100);
  EXPECT_FALSE(result.valid);
  EXPECT_EQ(result.new_update_index, 100);
}

// ============================================================================
// Continuous Depth Validation - Spot Tests
// ============================================================================

class SpotContinuousDepthTest : public ::testing::Test {
 protected:
  static constexpr MarketType kMarket = MarketType::kSpot;
};

TEST_F(SpotContinuousDepthTest, ValidWhenStartIsNextUpdateId) {
  // prev_u = 100, U = 101 -> valid (U == prev_u + 1)
  auto result = validate_continuous_depth(kMarket, 101, 110, 0, 100);
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(result.new_update_index, 110);
}

TEST_F(SpotContinuousDepthTest, InvalidWhenGapDetected) {
  // prev_u = 100, U = 105 -> invalid (gap of 4)
  auto result = validate_continuous_depth(kMarket, 105, 115, 0, 100);
  EXPECT_FALSE(result.valid);
  EXPECT_EQ(result.new_update_index, 100);  // keeps current
}

TEST_F(SpotContinuousDepthTest, InvalidWhenDuplicate) {
  // prev_u = 100, U = 100 -> invalid (duplicate)
  auto result = validate_continuous_depth(kMarket, 100, 110, 0, 100);
  EXPECT_FALSE(result.valid);
}

TEST_F(SpotContinuousDepthTest, InvalidWhenOutOfOrder) {
  // prev_u = 100, U = 99 -> invalid (out of order)
  auto result = validate_continuous_depth(kMarket, 99, 105, 0, 100);
  EXPECT_FALSE(result.valid);
}

TEST_F(SpotContinuousDepthTest, ValidWhenFirstMessageEver) {
  // update_index = 0 means first message, always accept
  auto result = validate_continuous_depth(kMarket, 50, 60, 0, 0);
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(result.new_update_index, 60);
}

TEST_F(SpotContinuousDepthTest, SpotIgnoresPrevEndIdx) {
  // Spot doesn't use prev_end_idx (pu), only start_idx (U)
  // Even if pu matches, if U doesn't match, it's invalid
  auto result = validate_continuous_depth(kMarket, 105, 115, 100, 100);
  EXPECT_FALSE(result.valid);  // U=105 != prev_u+1=101
}

// ============================================================================
// Continuous Depth Validation - Futures Tests
// ============================================================================

class FuturesContinuousDepthTest : public ::testing::Test {
 protected:
  static constexpr MarketType kMarket = MarketType::kFutures;
};

TEST_F(FuturesContinuousDepthTest, ValidWhenPuMatchesPrevU) {
  // prev_u = 100, pu = 100 -> valid (pu == prev_u)
  auto result = validate_continuous_depth(kMarket, 101, 110, 100, 100);
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(result.new_update_index, 110);
}

TEST_F(FuturesContinuousDepthTest, InvalidWhenPuDoesNotMatch) {
  // prev_u = 100, pu = 99 -> invalid
  auto result = validate_continuous_depth(kMarket, 101, 110, 99, 100);
  EXPECT_FALSE(result.valid);
  EXPECT_EQ(result.new_update_index, 100);  // keeps current
}

TEST_F(FuturesContinuousDepthTest, InvalidWhenPuHasGap) {
  // prev_u = 100, pu = 105 -> invalid (gap)
  auto result = validate_continuous_depth(kMarket, 106, 115, 105, 100);
  EXPECT_FALSE(result.valid);
}

TEST_F(FuturesContinuousDepthTest, ValidWhenFirstMessageEver) {
  // update_index = 0 means first message, always accept
  auto result = validate_continuous_depth(kMarket, 50, 60, 40, 0);
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(result.new_update_index, 60);
}

TEST_F(FuturesContinuousDepthTest, FuturesIgnoresStartIdx) {
  // Futures uses pu, not U for validation
  // Even if U doesn't follow +1 pattern, if pu matches, it's valid
  auto result = validate_continuous_depth(kMarket, 200, 210, 100, 100);
  EXPECT_TRUE(result.valid);  // pu=100 == prev_u=100
}

// ============================================================================
// Real-world Scenario Tests
// ============================================================================

class RealWorldScenarioTest : public ::testing::Test {};

TEST_F(RealWorldScenarioTest, SpotSequentialUpdates) {
  // Simulate a sequence of Spot depth updates
  uint64_t update_index = 0;

  // Snapshot with lastUpdateId = 1000
  update_index = 1000;

  // First depth after snapshot: U=998, u=1005 (overlaps)
  auto result1 = validate_first_depth_after_snapshot<MarketType::kSpot>(
      998, 1005, update_index);
  EXPECT_TRUE(result1.valid);
  update_index = result1.new_update_index;  // 1005

  // Next depth: U=1006, u=1010 (U == prev_u + 1)
  auto result2 =
      validate_continuous_depth(MarketType::kSpot, 1006, 1010, 0, update_index);
  EXPECT_TRUE(result2.valid);
  update_index = result2.new_update_index;  // 1010

  // Next depth: U=1011, u=1015
  auto result3 =
      validate_continuous_depth(MarketType::kSpot, 1011, 1015, 0, update_index);
  EXPECT_TRUE(result3.valid);
  update_index = result3.new_update_index;  // 1015

  EXPECT_EQ(update_index, 1015);
}

TEST_F(RealWorldScenarioTest, SpotImmediatelyFollowsSnapshot) {
  // When buffer is empty, first depth immediately follows snapshot
  uint64_t update_index = 1000;

  // First depth: U=1001, u=1010 (U == lastUpdateId + 1)
  auto result = validate_first_depth_after_snapshot<MarketType::kSpot>(
      1001, 1010, update_index);
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(result.new_update_index, 1010);
}

TEST_F(RealWorldScenarioTest, FuturesSequentialUpdates) {
  // Simulate a sequence of Futures depth updates
  uint64_t update_index = 0;

  // Snapshot with lastUpdateId = 1000
  update_index = 1000;

  // First depth after snapshot: U=998, u=1005 (overlaps)
  auto result1 = validate_first_depth_after_snapshot<MarketType::kFutures>(
      998, 1005, update_index);
  EXPECT_TRUE(result1.valid);
  update_index = result1.new_update_index;  // 1005

  // Next depth: U=1006, u=1020, pu=1005 (pu == prev_u)
  auto result2 = validate_continuous_depth(
      MarketType::kFutures, 1006, 1020, 1005, update_index);
  EXPECT_TRUE(result2.valid);
  update_index = result2.new_update_index;  // 1020

  // Next depth: U=1021, u=1030, pu=1020
  auto result3 = validate_continuous_depth(
      MarketType::kFutures, 1021, 1030, 1020, update_index);
  EXPECT_TRUE(result3.valid);
  update_index = result3.new_update_index;  // 1030

  EXPECT_EQ(update_index, 1030);
}

TEST_F(RealWorldScenarioTest, SpotGapDetectionAndRecovery) {
  uint64_t update_index = 1000;

  // Gap detected: expected U=1001, got U=1010
  auto result =
      validate_continuous_depth(MarketType::kSpot, 1010, 1020, 0, update_index);
  EXPECT_FALSE(result.valid);
  EXPECT_EQ(result.new_update_index, 1000);  // unchanged

  // After recovery (new snapshot with lastUpdateId=1015)
  update_index = 1015;

  // First depth after new snapshot: U=1010, u=1020 (overlaps)
  auto recovery_result = validate_first_depth_after_snapshot<MarketType::kSpot>(
      1010, 1020, update_index);
  EXPECT_TRUE(recovery_result.valid);
}

TEST_F(RealWorldScenarioTest, FuturesGapDetectionAndRecovery) {
  uint64_t update_index = 1000;

  // Gap detected: expected pu=1000, got pu=1005
  auto result = validate_continuous_depth(
      MarketType::kFutures, 1006, 1015, 1005, update_index);
  EXPECT_FALSE(result.valid);
  EXPECT_EQ(result.new_update_index, 1000);  // unchanged

  // After recovery (new snapshot with lastUpdateId=1020)
  update_index = 1020;

  // First depth after new snapshot: U=1015, u=1025 (overlaps)
  auto recovery_result =
      validate_first_depth_after_snapshot<MarketType::kFutures>(
          1015, 1025, update_index);
  EXPECT_TRUE(recovery_result.valid);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(EdgeCaseTest, LargeUpdateIds) {
  // Test with large uint64_t values
  uint64_t large_id = 9446683550037ULL;  // Real Binance Futures ID

  auto result = validate_first_depth_after_snapshot<MarketType::kSpot>(
      large_id - 100, large_id + 100, large_id);
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(result.new_update_index, large_id + 100);
}

TEST(EdgeCaseTest, ZeroUpdateIds) {
  // Edge case: all zeros - overlaps check passes
  auto result =
      validate_first_depth_after_snapshot<MarketType::kSpot>(0, 0, 0);
  // Spot: u <= lastUpdateId (0 <= 0) -> discarded
  EXPECT_FALSE(result.valid);

  // Futures: u < lastUpdateId (0 < 0 is false) -> not discarded, overlaps
  auto result_futures =
      validate_first_depth_after_snapshot<MarketType::kFutures>(0, 0, 0);
  EXPECT_TRUE(result_futures.valid);
}

TEST(EdgeCaseTest, OverflowProtection) {
  // Test near max uint64_t
  uint64_t max_val = UINT64_MAX;
  uint64_t near_max = max_val - 10;

  // This should not overflow
  auto result = validate_continuous_depth(
      MarketType::kSpot, near_max, max_val, 0, near_max - 1);
  EXPECT_TRUE(result.valid);
}

// ============================================================================
// Spot vs Futures Discard Difference
// ============================================================================

TEST(DiscardDifferenceTest, SpotDiscardsEndEqualToSnapshot) {
  // Spot: u <= lastUpdateId should be discarded
  // u == lastUpdateId -> discard
  auto result =
      validate_first_depth_after_snapshot<MarketType::kSpot>(90, 100, 100);
  EXPECT_FALSE(result.valid);
}

TEST(DiscardDifferenceTest, FuturesKeepsEndEqualToSnapshot) {
  // Futures: u < lastUpdateId should be discarded
  // u == lastUpdateId -> NOT discarded (overlaps)
  auto result =
      validate_first_depth_after_snapshot<MarketType::kFutures>(90, 100, 100);
  EXPECT_TRUE(result.valid);
}
