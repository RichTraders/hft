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

#include "market_data.h"
#include "order_entry.h"

namespace trading {
class MarketOrderBook;

template <typename Derived>
class BaseStrategy {
  void on_orderbook_updated(common::TickerId ticker_id, common::Price price,
                            common::Side side,
                            const MarketOrderBook* book) noexcept {
    static_cast<Derived>(this)->on_orderbook_updated(ticker_id, price, side,
                                                     book);
  }

  auto on_trade_updated(const MarketData* market_update,
                        MarketOrderBook* order_book) noexcept -> void {
    static_cast<Derived>(this)->on_trade_updated(market_update, order_book);
  }

  auto on_order_updated(const ExecutionReport* client_response) noexcept
      -> void {
    static_cast<Derived>(this)->on_order_updated(client_response);
  }
};
}  // namespace trading

#endif  //BASE_STRATEGY_H