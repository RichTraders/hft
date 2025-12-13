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

#include "fast_clock.h"
#include "ini_config.hpp"
#include "layer_book.h"
#include "logger.h"
#include "types.h"

namespace trading::order {
struct ActionNew {
  int layer;
  common::Price price;
  common::Qty qty;
  common::Side side;
  common::OrderId cl_order_id;
};

struct ActionReplace {
  int layer;
  common::Price price;
  common::Qty qty;
  common::Side side;
  common::OrderId cl_order_id;
  common::OrderId original_cl_order_id;
  common::Qty last_qty;
};

struct ActionCancel {
  int layer;
  common::Side side;
  common::OrderId cl_order_id;
  common::OrderId original_cl_order_id;
};

inline std::string toString(const ActionNew& action) {
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(kStringPrecision);
  stream << "ActionNew{" << "layer=" << action.layer << ", "
         << "price=" << action.price.value << ", " << "qty=" << action.qty.value
         << ", " << "side=" << common::toString(action.side) << ", "
         << "cl_order_id=" << common::toString(action.cl_order_id) << "}";
  return stream.str();
}

inline std::string toString(const ActionReplace& action) {
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(kStringPrecision);
  stream << "ActionReplace{" << "layer=" << action.layer << ", "
         << "price=" << action.price.value << ", " << "qty=" << action.qty.value
         << ", " << "side=" << common::toString(action.side) << ", "
         << "cl_order_id=" << common::toString(action.cl_order_id) << ", "
         << "original_cl_order_id="
         << common::toString(action.original_cl_order_id) << ", "
         << "last_qty=" << common::toString(action.last_qty) << "}";
  return stream.str();
}

inline std::string toString(const ActionCancel& action) {
  std::ostringstream stream;
  stream << "ActionCancel{" << "layer=" << action.layer << ", "
         << "side=" << common::toString(action.side) << ", "
         << "cl_order_id=" << common::toString(action.cl_order_id) << ", "
         << "original_cl_order_id="
         << common::toString(action.original_cl_order_id) << "}";
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
      : minimum_usdt_(INI_CONFIG.get_double("venue", "minimum_order_usdt")),
        minimum_qty_(INI_CONFIG.get_double("venue", "minimum_order_qty")),
        maximum_qty_(INI_CONFIG.get_double("venue", "maximum_order_qty")),
        minimum_time_gap_(
            INI_CONFIG.get_double("venue", "minimum_order_time_gap")),
        qty_increment_(kQtyDefault) {}

  ~VenuePolicy() = default;

  void set_qty_increment(double increment) { qty_increment_ = increment; }

  [[nodiscard]] common::Qty round_qty(common::Qty qty) const noexcept {
    const double steps = qty.value / qty_increment_;
    const double rounded = std::ceil(steps) * qty_increment_;
    return common::Qty{rounded};
  }

  void filter_by_venue(const std::string& symbol, Actions& actions,
      uint64_t current_time, LayerBook& layer_book) {
    uint64_t buy_last_used = 0;
    uint64_t sell_last_used = 0;
    std::tie(buy_last_used, sell_last_used) = layer_book.get_last_time(symbol);

    auto erase_side = [](auto& vec, common::Side side) {
      for (size_t i = 0; i < vec.size();) {
        if (vec[i].side == side) {
          vec[i] = std::move(vec.back());
          vec.pop_back();
        } else {
          ++i;
        }
      }
    };
    if (current_time - buy_last_used < minimum_time_gap_) {
      erase_side(actions.news, common::Side::kBuy);
      erase_side(actions.repls, common::Side::kBuy);
    }

    if (current_time - sell_last_used < minimum_time_gap_) {
      erase_side(actions.news, common::Side::kSell);
      erase_side(actions.repls, common::Side::kSell);
    }

    for (auto& action : actions.news) {
      action.qty.value =
          action.qty.value < minimum_qty_ ? minimum_qty_ : action.qty.value;

      const double order_usdt = action.price.value * action.qty.value;

      if (order_usdt < minimum_usdt_) {
        action.qty.value = minimum_usdt_ / action.price.value;
      }
      action.qty.value = std::min(maximum_qty_, action.qty.value);

      action.qty = round_qty(action.qty);
    }

    for (auto& action : actions.repls) {
      action.qty.value =
          action.qty.value < minimum_qty_ ? minimum_qty_ : action.qty.value;

      const double order_usdt = action.price.value * action.qty.value;

      if (order_usdt < minimum_usdt_) {
        action.qty.value = minimum_usdt_ / action.price.value;
      }
      action.qty.value = std::min(maximum_qty_, action.qty.value);

      action.qty = round_qty(action.qty);
      action.last_qty = round_qty(action.last_qty);
    }
  }

 private:
  static constexpr double kQtyDefault = 0.00001;
  const double minimum_usdt_;
  const double minimum_qty_;
  const double maximum_qty_;
  const uint64_t minimum_time_gap_;
  double qty_increment_;
};

struct TickConverter {
  double scale = 0;
  double inv = 0.0;
  static constexpr double kHalf = 0.5;
  static constexpr int kDigitMax = 9;
  static constexpr double kPower = 10.;
  static constexpr double kDiff = 1e-12;

  explicit TickConverter(double tick) noexcept {
    for (int digit = 0; digit <= kDigitMax; ++digit) {
      const double powered = std::pow(kPower, digit);
      if (std::abs((tick * powered) - 1.0) < kDiff) {
        scale = powered;
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
};

class QuoteReconciler {
 public:
  explicit QuoteReconciler(double tick_size)
      : min_replace_qty_delta_(
            INI_CONFIG.get_double("orders", "min_replace_qty_delta")),
        min_replace_tick_delta_(
            INI_CONFIG.get_uint64_t("orders", "min_replace_tick_delta")),
        tick_converter_(tick_size) {}

  Actions diff(const std::vector<QuoteIntent>& intents, LayerBook& layer_book,
      common::FastClock& clock) const {
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
      auto& side_book = layer_book.side_book(ticker_id, side);

      std::unordered_set<uint64_t> want_ticks;
      want_ticks.reserve(kSlotsPerSide);

      // bool did_victim_this_side = false;
      // bool active_intent = false;

      for (const auto& intent : intents) {
        if (intent.side != side)
          continue;
        const bool active =
            intent.price && intent.price->isValid() && intent.qty.value > 0;
        if (!active)
          continue;
        //active_intent = true;

        const uint64_t tick = tick_converter_.to_ticks(intent.price->value);
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
              .last_qty = vslot.qty});
          //did_victim_this_side = true;
          continue;
        }

        if (slot.state == OMOrderState::kInvalid ||
            slot.state == OMOrderState::kDead) {
          acts.news.push_back(ActionNew{.layer = assign.layer,
              .price = *intent.price,
              .qty = intent.qty,
              .side = side,
              .cl_order_id = common::OrderId{now}});
        } else if (slot.state == OMOrderState::kLive) {
          const auto slot_tick = tick_converter_.to_ticks(slot.price.value);
          const auto intent_tick =
              tick_converter_.to_ticks(intent.price->value);
          const bool price_diff =
              slot_tick > intent_tick
                  ? slot_tick - intent_tick >= min_replace_tick_delta_
                  : intent_tick - slot_tick >= min_replace_tick_delta_;
          const bool qty_diff = (std::abs(slot.qty.value - intent.qty.value) >=
                                 min_replace_qty_delta_);
          if (price_diff || qty_diff) {
            acts.repls.push_back(ActionReplace{.layer = assign.layer,
                .price = *intent.price,
                .qty = intent.qty,
                .side = side,
                .cl_order_id = common::OrderId{now},
                .original_cl_order_id = slot.cl_order_id,
                .last_qty = slot.qty});
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
  double min_replace_qty_delta_;
  uint64_t min_replace_tick_delta_;
  TickConverter tick_converter_;
};
}  // namespace trading::order
#endif  //QUOTE_RECONCILER_H