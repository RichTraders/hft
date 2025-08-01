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

#ifndef LIQUID_TAKER_H
#define LIQUID_TAKER_H
#include "strategy.hpp"

namespace trading {
class LiquidTaker : public BaseStrategy<LiquidTaker> {
  void on_orderbook_updated(const common::TickerId&, common::Price,
                            common::Side, const MarketOrderBook*) noexcept;

  void on_trade_updated(const MarketData*, MarketOrderBook*) noexcept;

  void on_order_updated(const ExecutionReport*) noexcept;
};
}  // namespace trading

#endif  //LIQUID_TAKER_H