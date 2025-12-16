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
template <typename Strategy, typename OeTraits>
class TradeEngine;

template <typename Strategy, typename OeTraits>
class OrderManager {
 public:
  using QuoteIntentType = typename Strategy::QuoteIntentType;

  static constexpr bool kSupportsCancelAndReorder =
      OeTraits::supports_cancel_and_reorder();
  static constexpr bool kSupportsPositionSide =
      OeTraits::supports_position_side();

  OrderManager(const common::Logger::Producer& logger,
      TradeEngine<Strategy, OeTraits>* trade_engine, RiskManager& risk_manager);
  ~OrderManager();
  void on_order_updated(const ExecutionReport* response) noexcept;
  void on_instrument_info(const InstrumentInfo& instrument_info) noexcept;

  void new_order(const common::TickerId& ticker_id, common::Price price,
      common::Side side, common::Qty qty, common::OrderId order_id,
      std::optional<common::PositionSide> position_side =
          std::nullopt) noexcept;

  void modify_order(const common::TickerId& ticker_id,
      const common::OrderId& order_id,
      const common::OrderId& cancel_new_order_id,
      const common::OrderId& original_order_id, common::Price price,
      common::Side side, common::Qty qty,
      std::optional<common::PositionSide> position_side =
          std::nullopt) noexcept;

  void cancel_order(const common::TickerId& ticker_id,
      const common::OrderId& original_order_id,
      std::optional<common::PositionSide> position_side =
          std::nullopt) noexcept;

  void apply(const std::vector<QuoteIntentType>& intents) noexcept;

  OrderManager() = delete;

  OrderManager(const OrderManager&) = delete;

  OrderManager(const OrderManager&&) = delete;

  OrderManager& operator=(const OrderManager&) = delete;

  OrderManager& operator=(const OrderManager&&) = delete;

 private:
  order::LayerBook layer_book_;
  TradeEngine<Strategy, OeTraits>* trade_engine_ = nullptr;
  RiskManager& risk_manager_;
  const common::Logger::Producer& logger_;
  common::FastClock fast_clock_;
  const double ticker_size_ = 0;
  order::QuoteReconciler<QuoteIntentType> reconciler_;
  order::VenuePolicy venue_policy_;
  order::TickConverter tick_converter_;

  OrderStateManager state_manager_;
  ReservedPositionTracker position_tracker_;
  OrderExpiryManager expiry_manager_;

  void filter_by_risk(const std::vector<QuoteIntentType>& intents,
      order::Actions& acts);

  void process_new_orders(const common::TickerId& ticker,
      order::Actions& actions, uint64_t now) noexcept;

  void process_replace_orders(const common::TickerId& ticker,
      order::Actions& actions, uint64_t now) noexcept;

  void process_cancel_orders(const common::TickerId& ticker,
      order::Actions& actions, uint64_t now) noexcept;

  void sweep_expired_orders(uint64_t now) noexcept;

  [[nodiscard]] common::OrderId gen_order_id() noexcept;

  void dump_all_slots(const std::string& symbol,
      std::string_view context) noexcept;
};
}  // namespace trading

#endif  //ORDER_MANAGER_H
