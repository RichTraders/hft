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

  void on_order_updated(const ExecutionReport* response) noexcept;

  void new_order(const common::TickerId& ticker_id, common::Price price,
                 common::Side side, common::Qty qty) noexcept;
  void modify_order(const common::TickerId& ticker_id,
                    const common::OrderId& order_id, common::Price price,
                    common::Side side, common::Qty qty) noexcept;

  void cancel_order(const common::TickerId& ticker_id,
                    const common::OrderId& order_id) noexcept;

  void apply(const std::vector<QuoteIntent>& intents) noexcept;

  OrderManager() = delete;

  OrderManager(const OrderManager&) = delete;

  OrderManager(const OrderManager&&) = delete;

  OrderManager& operator=(const OrderManager&) = delete;

  OrderManager& operator=(const OrderManager&&) = delete;

 private:
  order::LayerBook layer_book_;
  order::QuoteReconciler reconciler_;
  TradeEngine* trade_engine_ = nullptr;
  const RiskManager& risk_manager_;
  common::Logger* logger_ = nullptr;
  common::FastClock fast_clock_;

  void filter_by_risk(const std::vector<QuoteIntent>& intents,
                      order::Actions& acts) const;
};
}  // namespace trading

#endif  //ORDER_MANAGER_H