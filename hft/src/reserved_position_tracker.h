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

#ifndef RESERVED_POSITION_TRACKER_H
#define RESERVED_POSITION_TRACKER_H

#include "types.h"

namespace trading {

class ReservedPositionTracker {
 public:
  ReservedPositionTracker() = default;
  ~ReservedPositionTracker() = default;

  void add_reserved(common::Side side, common::Qty qty) noexcept {
    reserved_position_ += common::sideToValue(side) * qty;
    normalize();
  }

  void remove_reserved(common::Side side, common::Qty qty) noexcept {
    reserved_position_ -= common::sideToValue(side) * qty;
    normalize();
  }

  void remove_partial_fill(common::Side side, common::Qty filled_qty) noexcept {
    reserved_position_ -= common::sideToValue(side) * filled_qty;
    normalize();
  }

  [[nodiscard]] common::Qty get_reserved() const noexcept {
    return reserved_position_;
  }

  void reset() noexcept { reserved_position_ = common::Qty{0}; }

 private:
  void normalize() noexcept {
    constexpr double kReservedPositionEpsilon = 1e-8;
    if (std::abs(reserved_position_.value) < kReservedPositionEpsilon) {
      reserved_position_.value = 0.0;
    }
  }

  common::Qty reserved_position_{0};
};

}  // namespace trading

#endif  // RESERVED_POSITION_TRACKER_H
