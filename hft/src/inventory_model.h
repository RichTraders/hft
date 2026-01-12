#ifndef INVENTORY_MODEL_H_
#define INVENTORY_MODEL_H_

#include "common/fixed_point_config.hpp"
#include "common/types.h"

namespace trading {

// InventoryModel using fixed-point arithmetic
// position values use kQtyScale
// output (quote adjustment) uses kPriceScale
template <typename Derived>
class InventoryModel {
 public:
  [[nodiscard]] auto calculate_quote_adjustment(common::Side side,
      int64_t current_position,
      int64_t target_position = 0) const noexcept -> int64_t {
    return static_cast<const Derived*>(this)->calculate_quote_adjustment_impl(
        side,
        current_position,
        target_position);
  }

 protected:
  ~InventoryModel() = default;
};

class LinearSkewModel : public InventoryModel<LinearSkewModel> {
 public:
  // skew_coefficient is stored scaled by kPriceScale
  // e.g., coefficient=0.001 -> 0.001 * kPriceScale = 1000 (for kPriceScale=1M)
  // This represents: for each 1.0 qty position deviation, adjust price by 0.001

  // Constructor takes raw coefficient (e.g., 0.001) and scales it by kPriceScale
  explicit LinearSkewModel(double skew_coefficient) noexcept
      : skew_coefficient_scaled_(static_cast<int64_t>(
            skew_coefficient * common::FixedPointConfig::kPriceScale)) {}

  // Constructor for already-scaled coefficient
  static LinearSkewModel from_scaled(int64_t scaled_coefficient) noexcept {
    LinearSkewModel model(0.0);
    model.skew_coefficient_scaled_ = scaled_coefficient;
    return model;
  }

  // Returns price adjustment (scaled by kPriceScale)
  //
  // Unit analysis:
  //   skew_coefficient = 0.001 (price adjustment per 1.0 qty)
  //   skew_coefficient_scaled = 0.001 * kPriceScale (e.g., 1000 for kPriceScale=1M)
  //   position_deviation = actual_qty * kQtyScale (e.g., 100 qty -> 100'000)
  //
  // Calculation:
  //   actual_skew = coefficient * actual_qty = 0.001 * 100 = 0.1 (price)
  //   scaled_skew = 0.1 * kPriceScale = 100'000
  //
  // Formula:
  //   skew = coefficient_scaled * position_deviation / kQtyScale
  //        = (coefficient * kPriceScale) * (actual_qty * kQtyScale) / kQtyScale
  //        = coefficient * actual_qty * kPriceScale
  //        = actual_skew * kPriceScale ✓
  [[nodiscard]] auto calculate_quote_adjustment_impl(common::Side side,
      int64_t current_position,
      int64_t target_position) const noexcept -> int64_t {
    const int64_t position_deviation = current_position - target_position;
    const int64_t skew = (skew_coefficient_scaled_ * position_deviation) /
                         common::FixedPointConfig::kQtyScale;

    return (side == common::Side::kBuy) ? -skew : skew;
  }

  // Returns coefficient as double for logging
  [[nodiscard]] auto get_skew_coefficient() const noexcept -> double {
    return static_cast<double>(skew_coefficient_scaled_) /
           common::FixedPointConfig::kPriceScale;
  }

  // Returns scaled coefficient for internal use
  [[nodiscard]] auto get_skew_coefficient_scaled() const noexcept -> int64_t {
    return skew_coefficient_scaled_;
  }

  void set_skew_coefficient(double coefficient) noexcept {
    skew_coefficient_scaled_ = static_cast<int64_t>(
        coefficient * common::FixedPointConfig::kPriceScale);
  }

  void set_skew_coefficient_scaled(int64_t scaled) noexcept {
    skew_coefficient_scaled_ = scaled;
  }

 private:
  int64_t skew_coefficient_scaled_;
};
}  // namespace trading

#endif  // INVENTORY_MODEL_H_
