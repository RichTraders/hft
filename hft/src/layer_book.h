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

#ifndef LAYER_BOOK_H
#define LAYER_BOOK_H
#include "orders.h"

namespace trading::order {
struct OrderSlot {
  OMOrderState state{OMOrderState::kInvalid};
  common::Price price;
  common::Qty qty{0.0};
  uint64_t last_used{0};
  common::OrderId cl_order_id;
};
struct SideBook {
  std::array<OrderSlot, kSlotsPerSide> slots;
  std::array<uint64_t, kSlotsPerSide> layer_ticks;
  SideBook() { layer_ticks.fill(kTicksInvalid); }
};

struct AssignPlan {
  int layer{-1};
  std::optional<int> victim_live_layer;
  uint64_t tick;
};

class LayerBook {
 public:
  explicit LayerBook(const common::TickerId& ticker) {
    books_[ticker] = TwoSide{};
  }
  SideBook& side_book(const common::TickerId& ticker, common::Side side) {
    return books_[ticker][common::sideToIndex(side)];
  }

  static int find_layer_by_ticks(const SideBook& side_book,
                                 uint64_t tick) noexcept {
    for (int idx = 0; idx < kSlotsPerSide; ++idx)
      if (side_book.layer_ticks[idx] == tick)
        return idx;
    return -1;
  }
  static int find_layer_by_id(const SideBook& side_book,
                              const common::OrderId order_id) noexcept {
    if (order_id.value == common::kOrderIdInvalid)
      return -1;
    for (int idx = 0; idx < kSlotsPerSide; ++idx)
      if (side_book.slots[idx].cl_order_id.value == order_id.value)
        return idx;
    return -1;
  }

  static int find_free_layer(const SideBook& side_book) noexcept {
    for (int idx = 0; idx < kSlotsPerSide; ++idx) {
      const auto state = side_book.slots[idx].state;
      if (state == OMOrderState::kInvalid || state == OMOrderState::kDead)
        return idx;
      if (side_book.layer_ticks[idx] == kTicksInvalid)
        return idx;
    }
    return -1;
  }

  static int pick_victim_layer(const SideBook& side_book) noexcept {
    int victim = 0;
    for (int index = 1; index < kSlotsPerSide; ++index)
      if (side_book.slots[index].last_used < side_book.slots[victim].last_used)
        victim = index;
    return victim;
  }

  struct Assign {
    int layer{-1};
    std::optional<int> victim_live_layer;
  };
  static Assign get_or_assign_layer(SideBook& side_book, uint64_t tick,
                                    uint64_t now) {
    if (const int layer = find_layer_by_ticks(side_book, tick); layer >= 0) {
      side_book.slots[layer].last_used = now;
      side_book.slots[layer].cl_order_id = common::OrderId{now};
      return {.layer = layer, .victim_live_layer = std::nullopt};
    }
    if (const int free_layer = find_free_layer(side_book); free_layer >= 0) {
      side_book.layer_ticks[free_layer] = tick;
      side_book.slots[free_layer].last_used = now;
      side_book.slots[free_layer].cl_order_id = common::OrderId{now};
      return {.layer = free_layer, .victim_live_layer = std::nullopt};
    }
    int vidx = pick_victim_layer(side_book);
    std::optional<int> victim{};
    if (side_book.slots[vidx].state == OMOrderState::kLive)
      victim = vidx;
    side_book.layer_ticks[vidx] = tick;
    side_book.slots[vidx].last_used = now;
    side_book.slots[vidx].cl_order_id = common::OrderId{now};
    return {.layer = vidx, .victim_live_layer = victim};
  }

  static void unmap_layer(SideBook& side_book, int layer) {
    side_book.layer_ticks[layer] = kTicksInvalid;
  }

  static AssignPlan plan_layer(const SideBook& side_book, uint64_t tick) {
    if (const int layer = find_layer_by_ticks(side_book, tick); layer >= 0)
      return {.layer = layer, .victim_live_layer = std::nullopt, .tick = tick};
    if (const int layer = find_free_layer(side_book); layer >= 0)
      return {.layer = layer, .victim_live_layer = std::nullopt, .tick = tick};
    const int vidx = pick_victim_layer(side_book);
    std::optional<int> victim{};
    if (side_book.slots[vidx].state == OMOrderState::kLive)
      victim = vidx;
    return {.layer = vidx, .victim_live_layer = victim, .tick = tick};
  }

  std::pair<uint64_t, uint64_t> get_last_time(const std::string& symbol) {
    SideBook side = books_[symbol][common::sideToIndex(common::Side::kBuy)];
    uint64_t buy_last_time = 0;
    for (const auto& iter : side.slots) {
      buy_last_time = std::max(buy_last_time, iter.last_used);
    }

    side = books_[symbol][common::sideToIndex(common::Side::kSell)];
    uint64_t sell_last_time = 0;

    for (const auto& iter : side.slots) {
      sell_last_time = std::max(sell_last_time, iter.last_used);
    }

    return {buy_last_time, sell_last_time};
  }

 private:
  using TwoSide = std::array<SideBook, 2>;
  absl::flat_hash_map<std::string, TwoSide> books_;
};
}  // namespace trading::order
#endif  //LAYER_BOOK_H
