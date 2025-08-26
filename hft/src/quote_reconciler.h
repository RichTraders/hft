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
#include "layer_book.h"
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
        minimum_time_gap_(
            INI_CONFIG.get_double("venue", "minimum_order_time_gap")) {}

  ~VenuePolicy() = default;

  void filter_bu_venue(const std::string& symbol, Actions& actions,
                       uint64_t current_time, LayerBook& layer_book) {
    uint64_t buy_last_used = 0;
    uint64_t sell_last_used = 0;
    std::tie(buy_last_used, sell_last_used) = layer_book.get_last_time(symbol);

    if (current_time - buy_last_used < minimum_time_gap_) {
      for (size_t i = 0; i < actions.news.size();) {
        if (actions.news[i].side == common::Side::kBuy) {
          actions.news[i] = std::move(actions.news.back());
          actions.news.pop_back();
        } else {
          ++i;
        }
      }

      for (size_t i = 0; i < actions.repls.size();) {
        if (actions.repls[i].side == common::Side::kBuy) {
          actions.repls[i] = std::move(actions.repls.back());
          actions.repls.pop_back();
        } else {
          ++i;
        }
      }
    }

    if (current_time - sell_last_used < minimum_time_gap_) {
      for (size_t i = 0; i < actions.news.size();) {
        if (actions.news[i].side == common::Side::kSell) {
          actions.news[i] = std::move(actions.news.back());
          actions.news.pop_back();
        } else {
          ++i;
        }
      }

      for (size_t i = 0; i < actions.repls.size();) {
        if (actions.repls[i].side == common::Side::kSell) {
          actions.repls[i] = std::move(actions.repls.back());
          actions.repls.pop_back();
        } else {
          ++i;
        }
      }
    }

    for (auto& action : actions.news) {
      action.qty.value =
          action.qty.value < minimum_qty_ ? minimum_qty_ : action.qty.value;

      const double order_usdt = action.price.value * action.qty.value;

      if (order_usdt < minimum_usdt_) {
        action.qty.value = minimum_usdt_ / action.price.value;
      }
    }

    for (auto& action : actions.repls) {
      action.qty.value =
          action.qty.value < minimum_qty_ ? minimum_qty_ : action.qty.value;

      const double order_usdt = action.price.value * action.qty.value;

      if (order_usdt < minimum_usdt_) {
        action.qty.value = action.price.value / minimum_usdt_;
      }
    }
  }

 private:
  const double minimum_usdt_;
  const double minimum_qty_;
  const uint64_t minimum_time_gap_;
};

class QuoteReconciler {
 public:
  QuoteReconciler()
      : min_replace_qty_delta_(
            INI_CONFIG.get_double("orders", "min_replace_qty_delta")),
        min_replace_tick_delta_(
            INI_CONFIG.get_uint64_t("orders", "min_replace_tick_delta")) {}

  Actions diff(const std::vector<QuoteIntent>& intents, LayerBook& layer_book,
               double tick_size, common::FastClock& clock) const {
    Actions acts;
    if (intents.empty())
      return acts;

    const auto& ticker_id = intents.front().ticker;
    const uint64_t now = clock.get_timestamp();

    for (int side_index = 0; side_index < 2; ++side_index) {
      const common::Side side =
          (side_index == 0 ? common::Side::kBuy : common::Side::kSell);
      auto& side_book = layer_book.side_book(ticker_id, side);

      std::vector<uint64_t> want_ticks;
      want_ticks.reserve(kSlotsPerSide);

      bool did_victim_this_side = false;
      bool active_intent = false;

      for (const auto& intent : intents) {
        if (intent.side != side)
          continue;
        const bool active =
            intent.price && intent.price->isValid() && intent.qty.value > 0;
        if (!active)
          continue;
        active_intent = true;

        const uint64_t tick = to_ticks(intent.price->value, tick_size);
        want_ticks.push_back(tick);

        auto assign = LayerBook::plan_layer(side_book, tick);
        const OrderSlot& slot = side_book.slots[assign.layer];
        if (assign.victim_live_layer) {
          const int vidx = *assign.victim_live_layer;
          const auto& vslot = side_book.slots[vidx];
          acts.repls.push_back(
              ActionReplace{.layer = vidx,
                            .price = *intent.price,
                            .qty = intent.qty,
                            .side = side,
                            .cl_order_id = common::OrderId{now},
                            .original_cl_order_id = vslot.cl_order_id,
                            .last_qty = vslot.qty});
          did_victim_this_side = true;
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
          const auto slot_tick = to_ticks(slot.price.value, tick_size);
          const auto intent_tick = to_ticks(intent.price->value, tick_size);
          const bool price_diff =
              slot_tick > intent_tick
                  ? slot_tick - intent_tick >= min_replace_tick_delta_
                  : intent_tick - slot_tick >= min_replace_tick_delta_;
          const bool qty_diff = (std::abs(slot.qty.value - intent.qty.value) >=
                                 min_replace_qty_delta_);
          if (price_diff || qty_diff) {
            acts.repls.push_back(
                ActionReplace{.layer = assign.layer,
                              .price = *intent.price,
                              .qty = intent.qty,
                              .side = side,
                              .cl_order_id = common::OrderId{now},
                              .original_cl_order_id = slot.cl_order_id,
                              .last_qty = slot.qty});
          }
        }
      }

      if (active_intent && !did_victim_this_side) {
        for (int layer = 0; layer < kSlotsPerSide; ++layer) {
          if (side_book.slots[layer].state == OMOrderState::kLive) {
            const uint64_t tick = side_book.layer_ticks[layer];
            if (tick == kTicksInvalid)
              continue;
            if (std::ranges::find(want_ticks, tick) == want_ticks.end()) {
              acts.cancels.push_back(ActionCancel{
                  .layer = layer,
                  .side = side,
                  .cl_order_id = common::OrderId{now},
                  .original_cl_order_id = side_book.slots[layer].cl_order_id});
            }
          }
        }
      }
    }
    return acts;
  }

 private:
  double min_replace_qty_delta_;
  uint64_t min_replace_tick_delta_;
};
}  // namespace trading::order
#endif  //QUOTE_RECONCILER_H