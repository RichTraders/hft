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

#ifndef BASE_STRATEGY_H
#define BASE_STRATEGY_H

#include <types.h>

#include "logger.h"
#include "market_data.h"
#include "order_entry.h"

namespace trading {
class MarketOrderBook;
class FeatureEngine;
class OrderManager;

template <typename Derived>
class BaseStrategy {
 public:
  BaseStrategy(OrderManager* const order_manager,
               const FeatureEngine* const feature_engine,
               common::Logger* logger)
      : order_manager_(order_manager),
        feature_engine_(feature_engine),
        logger_(logger) {}

  void on_orderbook_updated(common::TickerId& ticker_id, common::Price price,
                            common::Side side,
                            const MarketOrderBook* book) noexcept {
    static_cast<Derived*>(this)->on_orderbook_updated(ticker_id, price, side,
                                                      book);
  }

  auto on_trade_updated(const MarketData* market_update,
                        MarketOrderBook* order_book) noexcept -> void {
    static_cast<Derived*>(this)->on_trade_updated(market_update, order_book);
  }

  auto on_order_updated(const ExecutionReport* client_response) noexcept
      -> void {
    static_cast<Derived*>(this)->on_order_updated(client_response);
  }

 protected:
  OrderManager* order_manager_;
  const FeatureEngine* feature_engine_;
  common::Logger* logger_;
};
}  // namespace trading

#endif  //BASE_STRATEGY_H