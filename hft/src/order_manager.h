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
#include "orders.h"
#include "quote_reconciler.h"

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
  common::Qty reserved_position_{0};

  uint64_t ttl_reserved_ns_;
  uint64_t ttl_live_ns_;

  order::TickConverter tick_converter_;

  struct ExpiryKey {
    uint64_t expire_ts{0};
    common::TickerId symbol;
    common::Side side{};
    uint32_t layer{0};
    common::OrderId cl_order_id;

    auto operator<=>(const ExpiryKey& key) const noexcept {
      return expire_ts <=> key.expire_ts;
    }
    friend std::ostream& operator<<(std::ostream& stream,
                                    const ExpiryKey& key) {
      stream << "expire_ts: " << key.expire_ts << ", symbol: " << key.symbol
             << ", side: " << common::toString(key.side)
             << ", layer: " << key.layer
             << ", cl_order_id: " << key.cl_order_id.value;
      return stream;
    }
  };

  // Manage expiry
  using MinHeap =
      std::priority_queue<ExpiryKey, std::vector<ExpiryKey>, std::greater<>>;
  MinHeap expiry_pq_;

  void filter_by_risk(const std::vector<QuoteIntent>& intents,
                      order::Actions& acts);

  void register_expiry(const common::TickerId& ticker, common::Side side,
                       uint32_t layer, const common::OrderId& order_id,
                       OMOrderState state) noexcept;

  void sweep_expired() noexcept;
  [[nodiscard]] common::OrderId gen_order_id() noexcept;
};
}  // namespace trading

#endif  //ORDER_MANAGER_H