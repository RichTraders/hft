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

#ifndef QUOTE_RECONCILER_H
#define QUOTE_RECONCILER_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "common/precision_config.hpp"
#include "fast_clock.h"
#include "ini_config.hpp"
#include "layer_book.h"
#include "logger.h"
#include "types.h"

namespace trading::order {
struct ActionNew {
  int layer;
  common::PriceType price;
  common::QtyType qty;
  common::Side side;
  common::OrderId cl_order_id;
  std::optional<common::PositionSide> position_side;
};

struct ActionReplace {
  int layer;
  common::PriceType price;
  common::QtyType qty;
  common::Side side;
  common::OrderId cl_order_id;
  common::OrderId original_cl_order_id;
  common::QtyType last_qty;
  std::optional<common::PositionSide> position_side;
};

struct ActionCancel {
  int layer;
  common::Side side;
  common::OrderId cl_order_id;
  common::OrderId original_cl_order_id;
  std::optional<common::PositionSide> position_side;
};

inline std::string toString(const ActionNew& action) {
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(PRECISION_CONFIG.qty_precision());
  stream << "ActionNew{" << "layer=" << action.layer << ", "
         << "price=" << action.price.to_double() << ", " << "qty=" << action.qty.to_double()
         << ", " << "side=" << common::toString(action.side) << ", "
         << "cl_order_id=" << common::toString(action.cl_order_id);
  if (action.position_side) {
    stream << ", position_side=" << common::toString(*action.position_side);
  }
  stream << "}";
  return stream.str();
}

inline std::string toString(const ActionReplace& action) {
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(PRECISION_CONFIG.qty_precision());
  stream << "ActionReplace{" << "layer=" << action.layer << ", "
         << "price=" << action.price.to_double() << ", " << "qty=" << action.qty.to_double()
         << ", " << "side=" << common::toString(action.side) << ", "
         << "cl_order_id=" << common::toString(action.cl_order_id) << ", "
         << "original_cl_order_id="
         << common::toString(action.original_cl_order_id) << ", "
         << "last_qty=" << common::toString(action.last_qty);
  if (action.position_side) {
    stream << ", position_side=" << common::toString(*action.position_side);
  }
  stream << "}";
  return stream.str();
}

inline std::string toString(const ActionCancel& action) {
  std::ostringstream stream;
  stream << "ActionCancel{" << "layer=" << action.layer << ", "
         << "side=" << common::toString(action.side) << ", "
         << "cl_order_id=" << common::toString(action.cl_order_id) << ", "
         << "original_cl_order_id="
         << common::toString(action.original_cl_order_id);
  if (action.position_side) {
    stream << ", position_side=" << common::toString(*action.position_side);
  }
  stream << "}";
  return stream.str();
}

struct Actions {
  std::vector<ActionNew> news;
  std::vector<ActionReplace> repls;
  std::vector<ActionCancel> cancels;

  [[nodiscard]] bool empty() const {
    return news.empty() && repls.empty() && cancels.empty();
  }
};

class VenuePolicy {
 public:
  VenuePolicy()
      : minimum_usdt_(INI_CONFIG.get_int64("venue", "minimum_order_usdt") *
                      common::FixedPointConfig::kPriceScale),
        minimum_qty_(INI_CONFIG.get_int64("venue", "minimum_order_qty") *
                     common::FixedPointConfig::kQtyScale),
        maximum_qty_(INI_CONFIG.get_int64("venue", "maximum_order_qty") *
                     common::FixedPointConfig::kQtyScale),
        minimum_time_gap_(
            INI_CONFIG.get_int64("venue", "minimum_order_time_gap")),
        qty_increment_(kQtyDefault) {}

  ~VenuePolicy() = default;

  void set_qty_increment(int64_t increment) { qty_increment_ = increment; }

  [[nodiscard]] common::QtyType round_qty(common::QtyType qty) const noexcept {
    const int64_t steps = (qty.value + qty_increment_ - 1) / qty_increment_;
    return common::QtyType::from_raw(steps * qty_increment_);
  }

  void filter_by_venue(const std::string& symbol, Actions& actions,
      uint64_t current_time, LayerBook& layer_book) const {
    auto last_times = layer_book.get_last_time(symbol);
    // [LONG_BUY, LONG_SELL, SHORT_BUY, SHORT_SELL]

    auto erase_if_too_recent = [&](auto& vec,
                                   common::Side side,
                                   std::optional<common::PositionSide>
                                       pos_side,
                                   size_t time_idx) {
      if (current_time - last_times[time_idx] < static_cast<uint64_t>(minimum_time_gap_)) {
        for (size_t i = 0; i < vec.size();) {
          if (vec[i].side == side && vec[i].position_side == pos_side) {
            vec[i] = std::move(vec.back());
            vec.pop_back();
          } else {
            ++i;
          }
        }
      }
    };

    // 4-way filtering: LONG_BUY, LONG_SELL, SHORT_BUY, SHORT_SELL
    erase_if_too_recent(actions.news,
        common::Side::kBuy,
        common::PositionSide::kLong,
        0);
    erase_if_too_recent(actions.news,
        common::Side::kSell,
        common::PositionSide::kLong,
        1);
    erase_if_too_recent(actions.news,
        common::Side::kBuy,
        common::PositionSide::kShort,
        2);
    erase_if_too_recent(actions.news,
        common::Side::kSell,
        common::PositionSide::kShort,
        3);

    erase_if_too_recent(actions.repls,
        common::Side::kBuy,
        common::PositionSide::kLong,
        0);
    erase_if_too_recent(actions.repls,
        common::Side::kSell,
        common::PositionSide::kLong,
        1);
    erase_if_too_recent(actions.repls,
        common::Side::kBuy,
        common::PositionSide::kShort,
        2);
    erase_if_too_recent(actions.repls,
        common::Side::kSell,
        common::PositionSide::kShort,
        3);

    erase_if_too_recent(actions.cancels,
        common::Side::kBuy,
        common::PositionSide::kLong,
        0);
    erase_if_too_recent(actions.cancels,
        common::Side::kSell,
        common::PositionSide::kLong,
        1);
    erase_if_too_recent(actions.cancels,
        common::Side::kBuy,
        common::PositionSide::kShort,
        2);
    erase_if_too_recent(actions.cancels,
        common::Side::kSell,
        common::PositionSide::kShort,
        3);

    for (auto& action : actions.news) {
      if (action.qty.value < minimum_qty_) {
        action.qty.value = minimum_qty_;
      }

      const int64_t order_usdt =
          action.price.value * action.qty.value /
          common::FixedPointConfig::kQtyScale;

      if (order_usdt < minimum_usdt_) {
        action.qty.value =
            minimum_usdt_ * common::FixedPointConfig::kQtyScale /
            action.price.value;
      }
      if (action.qty.value > maximum_qty_) {
        action.qty.value = maximum_qty_;
      }

      action.qty = round_qty(action.qty);
    }

    for (auto& action : actions.repls) {
      if (action.qty.value < minimum_qty_) {
        action.qty.value = minimum_qty_;
      }

      const int64_t order_usdt =
          action.price.value * action.qty.value /
          common::FixedPointConfig::kQtyScale;

      if (order_usdt < minimum_usdt_) {
        action.qty.value =
            minimum_usdt_ * common::FixedPointConfig::kQtyScale /
            action.price.value;
      }
      if (action.qty.value > maximum_qty_) {
        action.qty.value = maximum_qty_;
      }

      action.qty = round_qty(action.qty);
      action.last_qty = round_qty(action.last_qty);
    }
  }

 private:
  static constexpr int64_t kQtyDefault = 1;
  const int64_t minimum_usdt_;
  const int64_t minimum_qty_;
  const int64_t maximum_qty_;
  const int64_t minimum_time_gap_;
  int64_t qty_increment_;
};

struct TickConverter {
  double scale = 0;
  double inv = 0.0;
  static constexpr double kHalf = 0.5;
  static constexpr int kDigitMax = 9;
  static constexpr double kPower = 10.;
  static constexpr double kDiff = 1e-12;
  int64_t scale_int = 0;

  explicit TickConverter(double tick) noexcept {
    for (int digit = 0; digit <= kDigitMax; ++digit) {
      const double powered = std::pow(kPower, digit);
      if (std::abs((tick * powered) - 1.0) < kDiff) {
        scale = powered;
        // tick=0.1 -> scale=10, kPriceScale=10 -> scale_int = 10/10 = 1
        // price_raw / scale_int = ticks
        scale_int = common::FixedPointConfig::kPriceScale /
                    static_cast<int64_t>(powered);
        return;
      }
    }
    inv = 1.0 / tick;
  }

  [[nodiscard]] uint64_t to_ticks(double price) const noexcept {
    if (LIKELY(scale > 0.)) {
      return static_cast<uint64_t>((price * scale) + kHalf);
    }
    return std::llround(price * inv);
  }

  [[nodiscard]] uint64_t to_ticks_raw(int64_t price_raw) const noexcept {
    return static_cast<uint64_t>(price_raw / scale_int);
  }
};

template <typename QuoteIntentType>
class QuoteReconciler {
 public:
  explicit QuoteReconciler(double tick_size)
      : min_replace_qty_delta_(static_cast<int64_t>(
            INI_CONFIG.get_double("orders", "min_replace_qty_delta") *
            common::FixedPointConfig::kQtyScale)),
        min_replace_tick_delta_(
            INI_CONFIG.get_uint64_t("orders", "min_replace_tick_delta")),
        tick_converter_(tick_size) {}

  Actions diff(const std::vector<QuoteIntentType>& intents,
      LayerBook& layer_book, common::FastClock& clock) const {
    Actions acts;
    if (intents.empty()) {
      // TODO(SoftPull) Implement soft-pull
      // for (common::Side side : {common::Side::kBuy, common::Side::kSell}) {
      //   auto& sb = layer_book.side_book("BTCUSDT", side);
      //   for (int layer = 0; layer < kSlotsPerSide; ++layer) {
      //     const auto& slot = sb.slots[layer];
      //     if (slot.state != OMOrderState::kLive) continue;
      //
      //     const bool too_close = is_inside_bbo(sb, layer);     // 구현 예: 베스트와 tick<=N
      //     const bool too_old   = now - slot.last_used > age_ms_threshold_;
      //     const bool tiny_qty  = slot.qty.value < min_resting_qty_;
      //
      //     if (too_close || too_old || tiny_qty) {
      //       acts.cancels.push_back(ActionCancel{/*...*/});
      //     }
      //   }
      // }
      return acts;
    }
    const auto& ticker_id = intents.front().ticker;
    const uint64_t now = clock.get_timestamp();

    for (int side_index = 0; side_index < 2; ++side_index) {
      const common::Side side =
          (side_index == 0 ? common::Side::kBuy : common::Side::kSell);

      std::unordered_set<uint64_t> want_ticks;
      want_ticks.reserve(kSlotsPerSide);

      for (const auto& intent : intents) {
        if (intent.side != side)
          continue;

        std::optional<common::PositionSide> pos_side;
        if constexpr (requires { intent.position_side; }) {
          pos_side = intent.position_side;
        }

        auto& side_book = layer_book.side_book(ticker_id, side, pos_side);
        const bool active =
            intent.price && intent.price->is_valid() && intent.qty.value > 0;
        if (!active)
          continue;
        const uint64_t tick = tick_converter_.to_ticks_raw(intent.price->value);
        want_ticks.emplace(tick);

        auto assign = LayerBook::plan_layer(side_book, tick);
        const OrderSlot& slot = side_book.slots[assign.layer];
        if (assign.victim_live_layer) {
          const int vidx = *assign.victim_live_layer;
          const auto& vslot = side_book.slots[vidx];
          acts.repls.push_back(ActionReplace{.layer = vidx,
              .price = *intent.price,
              .qty = intent.qty,
              .side = side,
              .cl_order_id = common::OrderId{now},
              .original_cl_order_id = vslot.cl_order_id,
              .last_qty = vslot.qty,
              .position_side = pos_side});
          //did_victim_this_side = true;
          continue;
        }

        if (slot.state == OMOrderState::kInvalid ||
            slot.state == OMOrderState::kDead) {
          acts.news.push_back(ActionNew{.layer = assign.layer,
              .price = *intent.price,
              .qty = intent.qty,
              .side = side,
              .cl_order_id = common::OrderId{now},
              .position_side = pos_side});
        } else if (slot.state == OMOrderState::kLive) {
          const auto slot_tick = tick_converter_.to_ticks_raw(slot.price.value);
          const auto intent_tick = tick_converter_.to_ticks_raw(intent.price->value);
          const bool price_diff =
              slot_tick > intent_tick
                  ? slot_tick - intent_tick >= min_replace_tick_delta_
                  : intent_tick - slot_tick >= min_replace_tick_delta_;
          const int64_t qty_delta = slot.qty.value - intent.qty.value;
          const bool qty_diff = std::abs(qty_delta) >= min_replace_qty_delta_;
          if (price_diff || qty_diff) {
            acts.repls.push_back(ActionReplace{.layer = assign.layer,
                .price = *intent.price,
                .qty = intent.qty,
                .side = side,
                .cl_order_id = common::OrderId{now},
                .original_cl_order_id = slot.cl_order_id,
                .last_qty = slot.qty,
                .position_side = pos_side});
          }
        }
      }

      // Cancel all orders
      // if (active_intent && !did_victim_this_side) {
      //   for (int layer = 0; layer < kSlotsPerSide; ++layer) {
      //     if (side_book.slots[layer].state == OMOrderState::kLive) {
      //       const uint64_t tick = side_book.layer_ticks[layer];
      //       if (tick == kTicksInvalid)
      //         continue;
      //       if (std::ranges::find(want_ticks, tick) == want_ticks.end()) {
      //         acts.cancels.push_back(ActionCancel{
      //             .layer = layer,
      //             .side = side,
      //             .cl_order_id = common::OrderId{now},
      //             .original_cl_order_id = side_book.slots[layer].cl_order_id});
      //       }
      //     }
      //   }
      // }
    }
    return acts;
  }

 private:
  int64_t min_replace_qty_delta_;
  uint64_t min_replace_tick_delta_;
  TickConverter tick_converter_;
};
}  // namespace trading::order
#endif  //QUOTE_RECONCILER_H