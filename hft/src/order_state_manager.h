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

#ifndef ORDER_STATE_MANAGER_H
#define ORDER_STATE_MANAGER_H

#include <cstdint>

#include "layer_book.h"
#include "logger.h"
#include "market_data.h"
#include "order_entry.h"
#include "orders.h"
#include "quote_reconciler.h"
#include "reserved_position_tracker.h"

namespace trading {
class OrderStateManager {
 public:
  OrderStateManager(const common::Logger::Producer& logger,
      order::TickConverter& tick_converter)
      : logger_(logger), tick_converter_(tick_converter) {}

  ~OrderStateManager() = default;

  void handle_execution_report(const ExecutionReport* response,
      order::SideBook& side_book, ReservedPositionTracker& position_tracker,
      uint64_t now_ns) noexcept;

 private:
  void handle_pending_new(const ExecutionReport* response,
      order::SideBook& side_book) noexcept;

  void handle_new(const ExecutionReport* response,
      order::SideBook& side_book) noexcept;

  void handle_partially_filled(const ExecutionReport* response,
      order::SideBook& side_book, ReservedPositionTracker& position_tracker,
      uint64_t now_ns) noexcept;

  void handle_filled(const ExecutionReport* response,
      order::SideBook& side_book,
      ReservedPositionTracker& position_tracker) noexcept;

  void handle_pending_cancel(const ExecutionReport* response,
      order::SideBook& side_book) noexcept;

  void handle_canceled(const ExecutionReport* response,
      order::SideBook& side_book,
      ReservedPositionTracker& position_tracker) noexcept;

  void handle_rejected_or_expired(const ExecutionReport* response,
      order::SideBook& side_book,
      ReservedPositionTracker& position_tracker) noexcept;

  [[nodiscard]] int find_layer(const order::SideBook& side_book,
      const common::OrderId& order_id, common::PriceType price) const noexcept;

  const common::Logger::Producer& logger_;
  order::TickConverter& tick_converter_;
};

}  // namespace trading

#endif  // ORDER_STATE_MANAGER_H
