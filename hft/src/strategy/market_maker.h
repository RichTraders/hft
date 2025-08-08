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

#ifndef MARKETMAKER_H
#define MARKETMAKER_H
#include "strategy.hpp"

namespace trading {
class MarketMaker : public BaseStrategy<MarketMaker> {
  void on_orderbook_updated(common::TickerId, common::Price, common::Side,
                            const MarketOrderBook*) noexcept;

  void on_trade_updated(const MarketData*, MarketOrderBook*) noexcept;

  void on_order_updated(const ExecutionReport*) noexcept;
};
}  // namespace trading

#endif  //MARKETMAKER_H