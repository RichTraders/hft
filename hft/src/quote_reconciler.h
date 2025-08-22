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

#ifndef QUOTERECONCILER_H
#define QUOTERECONCILER_H
#include "fast_clock.h"
#include "layer_book.h"
#include "types.h"
namespace trading::order {
struct ActionNew {
  int layer;
  common::Price price;
  common::Qty qty;
  common::Side side;
};

struct ActionReplace {
  int layer;
  common::Price price;
  common::Qty qty;
  common::Side side;
  common::OrderId cl_order_id;
  common::Qty last_qty;
};

struct ActionCancel {
  int layer;
  common::Side side;
  common::OrderId cl_order_id;
};

struct Actions {
  std::vector<ActionNew> news;
  std::vector<ActionReplace> repls;
  std::vector<ActionCancel> cancels;
  [[nodiscard]] bool empty() const {
    return news.empty() && repls.empty() && cancels.empty();
  }
};

class QuoteReconciler {
 public:
  static Actions diff(const std::vector<QuoteIntent>& intents,
                      LayerBook& layer_book, double tick_size,
                      common::FastClock& clock) {
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

        auto assign = LayerBook::get_or_assign_layer(side_book, tick, now);
        const OrderSlot& slot = side_book.slots[assign.layer];
        if (assign.victim_live_layer) {
          const int vidx = *assign.victim_live_layer;
          const auto& vslot = side_book.slots[vidx];
          acts.repls.push_back(ActionReplace{.layer = vidx,
                                             .price = *intent.price,
                                             .qty = intent.qty,
                                             .side = side,
                                             .cl_order_id = vslot.cl_order_id,
                                             .last_qty = vslot.qty});
          did_victim_this_side = true;
          continue;
        }

        if (slot.state == OMOrderState::kInvalid ||
            slot.state == OMOrderState::kDead) {
          acts.news.push_back(ActionNew{.layer = assign.layer,
                                        .price = *intent.price,
                                        .qty = intent.qty,
                                        .side = side});
        } else if (slot.state == OMOrderState::kLive) {
          const auto slot_tick = to_ticks(slot.price.value, tick_size);
          const auto intent_tick = to_ticks(intent.price->value, tick_size);
          const bool price_diff =
              slot_tick > intent_tick
                  ? slot_tick - intent_tick >= kMinReplaceTickDelta
                  : intent_tick - slot_tick >= kMinReplaceTickDelta;
          const bool qty_diff = (std::abs(slot.qty.value - intent.qty.value) >=
                                 kMinReplaceQtyDelta);
          if (price_diff || qty_diff) {
            acts.repls.push_back(ActionReplace{.layer = assign.layer,
                                               .price = *intent.price,
                                               .qty = intent.qty,
                                               .side = side,
                                               .cl_order_id = slot.cl_order_id,
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
                  .cl_order_id = side_book.slots[layer].cl_order_id});
            }
          }
        }
      }
    }
    return acts;
  }
};
}  // namespace trading::order
#endif  //QUOTERECONCILER_H
