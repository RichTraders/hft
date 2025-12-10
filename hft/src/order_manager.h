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

#ifndef ORDER_MANAGER_H
#define ORDER_MANAGER_H
#include "risk_manager.h"

#include "fast_clock.h"
#include "layer_book.h"
#include "logger.h"
#include "market_data.h"
#include "order_expiry_manager.h"
#include "order_state_manager.h"
#include "orders.h"
#include "quote_reconciler.h"
#include "reserved_position_tracker.h"

namespace trading {
template <typename Strategy>
class TradeEngine;

template <typename Strategy>
class OrderManager {
 public:
  OrderManager(common::Logger* logger, TradeEngine<Strategy>* trade_engine,
               RiskManager& risk_manager);
  ~OrderManager();
  void on_order_updated(const ExecutionReport* response) noexcept;
  void on_instrument_info(const InstrumentInfo& instrument_info) noexcept;

  void new_order(const common::TickerId& ticker_id, common::Price price,
                 common::Side side, common::Qty qty,
                 common::OrderId order_id) noexcept;
  void modify_order(const common::TickerId& ticker_id,
                    const common::OrderId& order_id,
                    const common::OrderId& cancel_new_order_id,
                    const common::OrderId& original_order_id,
                    common::Price price, common::Side side,
                    common::Qty qty) noexcept;

  void cancel_order(const common::TickerId& ticker_id,
                    const common::OrderId& original_order_id,
                    const common::OrderId& order_id) noexcept;

  void apply(const std::vector<QuoteIntent>& intents) noexcept;

  OrderManager() = delete;

  OrderManager(const OrderManager&) = delete;

  OrderManager(const OrderManager&&) = delete;

  OrderManager& operator=(const OrderManager&) = delete;

  OrderManager& operator=(const OrderManager&&) = delete;

 private:
  order::LayerBook layer_book_;
  TradeEngine<Strategy>* trade_engine_ = nullptr;
  RiskManager& risk_manager_;
  common::Logger::Producer logger_;
  common::FastClock fast_clock_;
  const double ticker_size_ = 0;
  order::QuoteReconciler reconciler_;
  order::VenuePolicy venue_policy_;
  order::TickConverter tick_converter_;

  // Delegated components for separation of concerns
  OrderStateManager state_manager_;
  ReservedPositionTracker position_tracker_;
  OrderExpiryManager expiry_manager_;

  void filter_by_risk(const std::vector<QuoteIntent>& intents,
                      order::Actions& acts);

  [[nodiscard]] common::OrderId gen_order_id() noexcept;

  void dump_all_slots(const std::string& symbol,
                      const std::string& context) noexcept;
};
}  // namespace trading

#endif  //ORDER_MANAGER_H