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
#include "orders.h"

namespace trading {
class TradeEngine;

class OrderManager {
 public:
  OrderManager(common::Logger* logger, TradeEngine* trade_engine,
               RiskManager& risk_manager);

  void on_order_updated(const ExecutionReport* client_response) noexcept;

  void new_order(Order* order, const common::TickerId& ticker_id,
                 common::Price price, common::Side side,
                 common::Qty qty) noexcept;

  void cancel_order(Order* order) noexcept;

  void move_order(Order* order, const common::TickerId& ticker_id,
                  common::Price price, common::Side side,
                  common::Qty qty) noexcept;

  void move_order(const common::TickerId& ticker_id, common::Price bid_price,
                  common::Side side, const common::Qty& qty) noexcept;

  static bool isWorking(const OMOrderState state) noexcept {
    return (state == OMOrderState::kLive);
  }

  OrderManager() = delete;

  OrderManager(const OrderManager&) = delete;

  OrderManager(const OrderManager&&) = delete;

  OrderManager& operator=(const OrderManager&) = delete;

  OrderManager& operator=(const OrderManager&&) = delete;

 private:
  TradeEngine* trade_engine_ = nullptr;
  const RiskManager& risk_manager_;
  common::Logger* logger_ = nullptr;
  OMOrderTickerSideHashMap ticker_side_order_;
  common::FastClock fast_clock_;

  Order* find_order(const std::string& ticker, common::Side side,
                    common::OrderId order_id);

  Order* prepare_order(const std::string& ticker, common::Side side,
                       bool create_if_missing = true);
};
}  // namespace trading

#endif  //ORDER_MANAGER_H