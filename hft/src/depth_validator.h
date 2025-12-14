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

#ifndef DEPTH_VALIDATOR_H
#define DEPTH_VALIDATOR_H

namespace trading {

enum class MarketType : uint8_t { kSpot, kFutures };

struct DepthValidationResult {
  bool valid;
  uint64_t new_update_index;
};

// Pure function for first depth after snapshot validation
// Both Spot and Futures: U <= lastUpdateId AND u >= lastUpdateId
constexpr DepthValidationResult validate_first_depth_after_snapshot(
    uint64_t start_idx, uint64_t end_idx, uint64_t snapshot_update_id) {
  const bool valid =
      (start_idx <= snapshot_update_id) && (end_idx >= snapshot_update_id);
  return {valid, valid ? end_idx : snapshot_update_id};
}

constexpr DepthValidationResult validate_continuous_depth(
    MarketType market_type, uint64_t start_idx, uint64_t end_idx,
    uint64_t prev_end_idx, uint64_t current_update_index) {
  bool valid = false;

  if (current_update_index == 0) {
    // First message ever, accept it
    valid = true;
  } else if (market_type == MarketType::kFutures) {
    // Futures: pu == prev_u
    valid = (prev_end_idx == current_update_index);
  } else {
    // Spot: U == prev_u + 1
    valid = (start_idx == current_update_index + 1);
  }

  return {valid, valid ? end_idx : current_update_index};
}

constexpr MarketType to_market_type(std::string_view market_type_str) {
  if (market_type_str == "Futures") {
    return MarketType::kFutures;
  }
  return MarketType::kSpot;
}

template <typename ExchangeTraits>
constexpr MarketType get_market_type() {
  if constexpr (ExchangeTraits::market_type() == "Futures") {
    return MarketType::kFutures;
  }
  return MarketType::kSpot;
}

}  // namespace trading

#endif  // DEPTH_VALIDATOR_H
