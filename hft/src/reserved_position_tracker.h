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

#include <cmath>

#include "types.h"

namespace trading {

class ReservedPositionTracker {
 public:
  ReservedPositionTracker() = default;
  ~ReservedPositionTracker() = default;

  void add_reserved(common::Side side, int64_t qty) noexcept {
    reserved_position_ += common::sideToValue(side) * qty;
  }

  void remove_reserved(common::Side side, int64_t qty) noexcept {
    reserved_position_ -= common::sideToValue(side) * qty;
  }

  void remove_partial_fill(common::Side side, int64_t filled_qty) noexcept {
    reserved_position_ -= common::sideToValue(side) * filled_qty;
  }

  [[nodiscard]] int64_t get_reserved() const noexcept {
    return reserved_position_;
  }

  void reset() noexcept { reserved_position_ = 0; }

 private:
  int64_t reserved_position_{0};
};

}  // namespace trading

#endif  // RESERVED_POSITION_TRACKER_H
