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

namespace trading {
class MarketOrderBook;

template <typename Derived>
class BaseStrategy {
  void onOrderBookUpdate(common::TickerId ticker_id, common::Price price,
                         common::Side side,
                         const MarketOrderBook* book) noexcept {
    static_cast<Derived>(this)->onOrderBookUpdate(ticker_id, price, side, book);
  }
};
}  // namespace trading

#endif  //BASE_STRATEGY_H