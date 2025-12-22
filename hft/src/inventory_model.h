#pragma once

#include "common/types.h"

namespace trading {

template <typename Derived>
class InventoryModel {
 public:
  [[nodiscard]] auto calculate_quote_adjustment(common::Side side,
      double current_position,
      double target_position = 0.0) const noexcept -> double {
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
  explicit LinearSkewModel(double skew_coefficient) noexcept
      : skew_coefficient_(skew_coefficient) {}

  [[nodiscard]] auto calculate_quote_adjustment_impl(common::Side side,
      double current_position,
      double target_position) const noexcept -> double {
    const double position_deviation = current_position - target_position;
    const double skew = skew_coefficient_ * position_deviation;

    return (side == common::Side::kBuy) ? -skew : skew;
  }

  [[nodiscard]] auto get_skew_coefficient() const noexcept -> double {
    return skew_coefficient_;
  }

  void set_skew_coefficient(double coefficient) noexcept {
    skew_coefficient_ = coefficient;
  }

 private:
  double skew_coefficient_;
};
}  // namespace trading
