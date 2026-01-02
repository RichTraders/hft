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

#include <cstdint>
#include <optional>

#include "order_entry.h"
#include "order_state_manager.h"

namespace trading {

using order::LayerBook;

void OrderStateManager::handle_execution_report(const ExecutionReport* response,
    order::SideBook& side_book, ReservedPositionTracker& position_tracker,
    uint64_t now_ns) noexcept {

  switch (response->ord_status) {
    case OrdStatus::kPendingNew:
      handle_pending_new(response, side_book);
      break;
    case OrdStatus::kNew:
      handle_new(response, side_book);
      break;
    case OrdStatus::kPartiallyFilled:
      handle_partially_filled(response, side_book, position_tracker, now_ns);
      break;
    case OrdStatus::kFilled:
      handle_filled(response, side_book, position_tracker);
      break;
    case OrdStatus::kPendingCancel:
      handle_pending_cancel(response, side_book);
      break;
    case OrdStatus::kCanceled:
      handle_canceled(response, side_book, position_tracker);
      break;
    case OrdStatus::kRejected:
      [[fallthrough]];
    case OrdStatus::kExpired:
      handle_rejected_or_expired(response, side_book, position_tracker);
      break;
    default:
      logger_.error("[OrderStateManager] Unknown OrdStatus {}",
          toString(response->ord_status));
      break;
  }
}

void OrderStateManager::handle_pending_new(const ExecutionReport* response,
    order::SideBook& side_book) noexcept {
  const int layer =
      find_layer(side_book, response->cl_order_id, response->price);
  if (layer < 0) {
    logger_.error("[OrderStateManager] PendingNew: layer not found {}",
        response->toString());
    return;
  }
  auto& slot = side_book.slots[layer];
  slot.state = OMOrderState::kPendingNew;
  logger_.info("[OrderStateManager] PendingNew {}", response->toString());
}

void OrderStateManager::handle_new(const ExecutionReport* response,
    order::SideBook& side_book) noexcept {
  int layer = -1;

  {
    const auto iter =
        side_book.new_id_to_layer.find(response->cl_order_id.value);
    if (iter != side_book.new_id_to_layer.end())
      layer = iter->second;
  }

  if (layer < 0) {
    layer = find_layer(side_book, response->cl_order_id, response->price);
  }

  if (layer < 0) {
    logger_.error("[OrderStateManager] New: layer not found {}",
        response->toString());
    return;
  }

  auto& new_slot = side_book.slots[layer];

  // Case: cancel and reorder (replace operation)
  if (auto& pend_opt = side_book.pending_repl[layer]; pend_opt.has_value()) {
    const auto& pend = *pend_opt;

    side_book.layer_ticks[layer] = pend.new_tick;
    new_slot.price = response->price;
    new_slot.qty = response->leaves_qty;
    new_slot.cl_order_id = response->cl_order_id;
    new_slot.state = OMOrderState::kLive;
    side_book.pending_repl[layer].reset();
    side_book.new_id_to_layer.erase(response->cl_order_id.value);
  } else {
    // Case: general new order
    side_book.layer_ticks[layer] =
        tick_converter_.to_ticks_raw(response->price.value);
    new_slot.price = response->price;
    new_slot.qty = response->leaves_qty;
    new_slot.cl_order_id = response->cl_order_id;
    new_slot.state = OMOrderState::kLive;
  }

  logger_.info("[OrderStateManager] New {}", response->toString());
}

void OrderStateManager::handle_partially_filled(const ExecutionReport* response,
    order::SideBook& side_book, ReservedPositionTracker& position_tracker,
    uint64_t now_ns) noexcept {
  const int layer =
      find_layer(side_book, response->cl_order_id, response->price);
  if (layer < 0) {
    logger_.error("[OrderStateManager] PartiallyFilled: layer not found {}",
        response->toString());
    return;
  }

  auto& slot = side_book.slots[layer];
  const int64_t filled_qty = slot.qty.value - response->leaves_qty.value;
  position_tracker.remove_partial_fill(response->side, filled_qty);
  slot.qty = response->leaves_qty;
  slot.state = (response->leaves_qty.value <= 0) ? OMOrderState::kDead
                                                 : OMOrderState::kLive;

  if (slot.state == OMOrderState::kDead) {
    LayerBook::unmap_layer(side_book, layer);
  } else {
    slot.last_used = now_ns;
  }

  logger_.info("[OrderStateManager] PartiallyFilled {}", response->toString());
}

void OrderStateManager::handle_filled(const ExecutionReport* response,
    order::SideBook& side_book,
    ReservedPositionTracker& position_tracker) noexcept {
  const int layer =
      find_layer(side_book, response->cl_order_id, response->price);
  if (layer < 0) {
    logger_.error("[OrderStateManager] Filled: layer not found {}",
        response->toString());
    return;
  }

  auto& slot = side_book.slots[layer];
  position_tracker.remove_reserved(response->side, slot.qty.value);
  slot.qty = response->leaves_qty;
  slot.state = OMOrderState::kDead;
  LayerBook::unmap_layer(side_book, layer);

  logger_.info("[OrderStateManager] Filled {}", response->toString());
}

void OrderStateManager::handle_pending_cancel(const ExecutionReport* response,
    order::SideBook& side_book) noexcept {
  const int layer =
      find_layer(side_book, response->cl_order_id, response->price);
  if (layer < 0) {
    logger_.error("[OrderStateManager] PendingCancel: layer not found {}",
        response->toString());
    return;
  }

  auto& slot = side_book.slots[layer];
  slot.state = OMOrderState::kPendingCancel;
  logger_.info("[OrderStateManager] PendingCancel {}", response->toString());
}

void OrderStateManager::handle_canceled(const ExecutionReport* response,
    order::SideBook& side_book,
    ReservedPositionTracker& position_tracker) noexcept {
  int layer;
  if (const auto iter =
          side_book.orig_id_to_layer.find(response->cl_order_id.value);
      iter != side_book.orig_id_to_layer.end()) {
    layer = iter->second;
    side_book.orig_id_to_layer.erase(iter);
    auto& slot = side_book.slots[layer];
    slot.state = OMOrderState::kReserved;
    logger_.info("[OrderStateManager] Canceled (for replace) {}",
        response->toString());
    return;
  }

  layer = find_layer(side_book, response->cl_order_id, response->price);
  if (layer < 0) {
    logger_.error("[OrderStateManager] Canceled: layer not found {}",
        response->toString());
    return;
  }

  auto& slot = side_book.slots[layer];
  position_tracker.remove_reserved(response->side, slot.qty.value);
  slot.state = OMOrderState::kDead;
  LayerBook::unmap_layer(side_book, layer);

  logger_.info("[OrderStateManager] Canceled {}", response->toString());
}

void OrderStateManager::handle_rejected_or_expired(
    const ExecutionReport* response, order::SideBook& side_book,
    ReservedPositionTracker& position_tracker) noexcept {
  int layer = -1;

  // Check if this is a rejected replace operation
  if (const auto iter =
          side_book.new_id_to_layer.find(response->cl_order_id.value);
      iter != side_book.new_id_to_layer.end()) {
    layer = iter->second;
  }

  // Handle replace rejection: restore original order state
  if (const auto& pend_opt = side_book.pending_repl[layer];
      layer >= 0 && pend_opt.has_value()) {
    const auto& pend = *pend_opt;
    const auto delta_qty = pend.new_qty.value - pend.last_qty.value;
    position_tracker.remove_reserved(response->side, delta_qty);

    side_book.pending_repl[layer].reset();
    if (const auto iter =
            side_book.new_id_to_layer.find(response->cl_order_id.value);
        iter != side_book.new_id_to_layer.end()) {
      side_book.new_id_to_layer.erase(iter);
    }

    auto& slot = side_book.slots[layer];
    slot.state = OMOrderState::kLive;
    slot.price = pend.original_price;
    slot.cl_order_id = pend.original_cl_order_id;
    slot.qty = pend.last_qty;
    side_book.layer_ticks[layer] = pend.original_tick;

    const auto cancel_id = response->cl_order_id.value - 1;
    if (const auto iter = side_book.orig_id_to_layer.find(cancel_id);
        iter != side_book.orig_id_to_layer.end()) {
      side_book.orig_id_to_layer.erase(iter);
    }

    logger_.info(
        "[OrderStateManager] Rejected (replace failed, restored original "
        "oid={}, price={:.2f}, qty={:.6f}) {}",
        pend.original_cl_order_id.value,
        pend.original_price.to_double(),
        pend.last_qty.to_double(),
        response->toString());
  } else {
    layer = find_layer(side_book, response->cl_order_id, response->price);
    if (layer >= 0) {
      auto& slot = side_book.slots[layer];
      position_tracker.remove_reserved(response->side, slot.qty.value);
      slot.state = OMOrderState::kDead;
      LayerBook::unmap_layer(side_book, layer);
    } else {
      logger_.error("[OrderStateManager] {}: layer not found {}",
          trading::toString(response->ord_status),
          response->toString());
    }
  }

  logger_.error("[OrderStateManager] {} {}",
      trading::toString(response->ord_status),
      response->toString());
}

int OrderStateManager::find_layer(const order::SideBook& side_book,
    const common::OrderId& order_id, common::PriceType price) const noexcept {
  const int layer = LayerBook::find_layer_by_id(side_book, order_id);
  if (layer >= 0)
    return layer;

  const uint64_t tick = tick_converter_.to_ticks_raw(price.value);
  return LayerBook::find_layer_by_ticks(side_book, tick);
}

}  // namespace trading
