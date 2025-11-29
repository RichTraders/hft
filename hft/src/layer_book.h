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
#include "absl/container/flat_hash_map.h"
#include "orders.h"
constexpr int kStringPrecision = 5;

namespace trading::order {
struct OrderSlot {
  OMOrderState state{OMOrderState::kInvalid};
  common::Price price;
  common::Qty qty;
  uint64_t last_used{0};
  common::OrderId cl_order_id;
  OrderSlot() = default;
};

inline std::string toString(const OrderSlot& slot) {
  std::ostringstream ostream;
  ostream << std::fixed << std::setprecision(kStringPrecision);
  ostream << "OrderSlot{" << "state=" << toString(slot.state) << ", "
          << "price=" << slot.price.value << ", " << "qty=" << slot.qty.value
          << ", " << "last_used=" << slot.last_used << ", "
          << "cl_order_id=" << slot.cl_order_id.value << "}";
  return ostream.str();
}

struct PendingReplaceInfo {
  common::Price new_price;
  common::Qty new_qty;
  uint64_t new_tick;
  common::OrderId new_cl_order_id;
  common::Qty last_qty;
  common::OrderId original_cl_order_id;
  common::Price original_price;
  uint64_t original_tick;
  PendingReplaceInfo() = default;
  PendingReplaceInfo(const common::Price& new_price, const common::Qty& new_qty,
      uint64_t new_tick, const common::OrderId& new_cl_order_id,
      const common::Qty& last_qty, const common::OrderId& original_cl_order_id,
      const common::Price& original_price, uint64_t original_tick)
      : new_price(new_price),
        new_qty(new_qty),
        new_tick(new_tick),
        new_cl_order_id(new_cl_order_id),
        last_qty(last_qty),
        original_cl_order_id(original_cl_order_id),
        original_price(original_price),
        original_tick(original_tick) {}
};

struct SideBook {
  std::array<OrderSlot, kSlotsPerSide> slots;
  std::array<uint64_t, kSlotsPerSide> layer_ticks;
  std::array<std::optional<PendingReplaceInfo>, kSlotsPerSide> pending_repl;
  absl::flat_hash_map<uint64_t, int> orig_id_to_layer;
  absl::flat_hash_map<uint64_t, int> new_id_to_layer;
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
    books_.reserve(1);
    books_.try_emplace(ticker, TwoSide{});
  }
  LayerBook(LayerBook&&) = delete;
  LayerBook& operator=(LayerBook&&) = delete;
  LayerBook(LayerBook const&) = delete;
  LayerBook& operator=(LayerBook const&) = delete;

  SideBook& side_book(const common::TickerId& ticker, common::Side side) {
    auto book = books_.find(ticker);
    if (book == books_.end()) {
      book = books_.try_emplace(ticker, TwoSide{}).first;
    }
    return book->second[common::sideToIndex(side)];
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

  static void unmap_layer(SideBook& side_book, int layer) {
    side_book.layer_ticks[layer] = kTicksInvalid;

    for (auto iter = side_book.new_id_to_layer.begin();
         iter != side_book.new_id_to_layer.end();) {
      if (iter->second == layer) {
        const auto to_erase = iter;
        ++iter;
        side_book.new_id_to_layer.erase(to_erase);
      } else
        ++iter;
    }
    for (auto iter = side_book.orig_id_to_layer.begin();
         iter != side_book.orig_id_to_layer.end();) {
      if (iter->second == layer) {
        const auto to_erase = iter;
        ++iter;
        side_book.orig_id_to_layer.erase(to_erase);
      } else
        ++iter;
    }
    side_book.pending_repl[layer].reset();
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
    auto is_active = [](const auto& slot) {
      return slot.state == OMOrderState::kLive ||
             slot.state == OMOrderState::kReserved;
    };

    auto last_time_for = [&](common::Side side_enum) -> uint64_t {
      const auto& side = books_[symbol][common::sideToIndex(side_enum)];
      const auto& slots = side.slots;

      uint64_t last = 0;
      for (const auto& slot : slots | std::ranges::views::filter(is_active)) {
        last = std::max(last, slot.last_used);
      }

      return last;
    };

    const uint64_t buy_last_time = last_time_for(common::Side::kBuy);
    const uint64_t sell_last_time = last_time_for(common::Side::kSell);

    return {buy_last_time, sell_last_time};
  }

 private:
  using TwoSide = std::array<SideBook, 2>;
  absl::flat_hash_map<std::string, TwoSide> books_;
};
}  // namespace trading::order
#endif  //LAYER_BOOK_H
