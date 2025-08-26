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
#include "orders.h"
#include "quote_reconciler.h"

namespace trading {
class TradeEngine;

class OrderManager {
 public:
  OrderManager(common::Logger* logger, TradeEngine* trade_engine,
               RiskManager& risk_manager);
  ~OrderManager();
  void on_order_updated(const ExecutionReport* response) noexcept;

  void new_order(const common::TickerId& ticker_id, common::Price price,
                 common::Side side, common::Qty qty,
                 common::OrderId order_id) const noexcept;
  void modify_order(const common::TickerId& ticker_id,
                    const common::OrderId& order_id,
                    const common::OrderId& original_order_id,
                    common::Price price, common::Side side,
                    common::Qty qty) const noexcept;

  void cancel_order(const common::TickerId& ticker_id,
                    const common::OrderId& original_order_id,
                    const common::OrderId& order_id) const noexcept;

  void apply(const std::vector<QuoteIntent>& intents) noexcept;

  OrderManager() = delete;

  OrderManager(const OrderManager&) = delete;

  OrderManager(const OrderManager&&) = delete;

  OrderManager& operator=(const OrderManager&) = delete;

  OrderManager& operator=(const OrderManager&&) = delete;

 private:
  order::LayerBook layer_book_;
  TradeEngine* trade_engine_ = nullptr;
  const RiskManager& risk_manager_;
  common::Logger* logger_ = nullptr;
  common::FastClock fast_clock_;
  order::QuoteReconciler reconciler_;
  const double ticker_size_ = 0;
  common::Qty reserved_position_{0};

  void filter_by_risk(const std::vector<QuoteIntent>& intents,
                      order::Actions& acts) const;
};
}  // namespace trading

#endif  //ORDER_MANAGER_H