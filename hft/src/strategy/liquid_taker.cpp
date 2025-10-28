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

#include "liquid_taker.h"

#include "feature_engine.h"
#include "order_book.h"
#include "order_manager.h"
#include "strategy_dispatch.hpp"

namespace trading {
LiquidTaker::LiquidTaker(OrderManager* const order_manager,
                         const FeatureEngine* const feature_engine,
                         common::Logger* logger,
                         const common::TradeEngineCfgHashMap&)
    : BaseStrategy(order_manager, feature_engine, logger) {}

void LiquidTaker::on_orderbook_updated(const common::TickerId&, common::Price,
                                       common::Side,
                                       const MarketOrderBook*) noexcept {}

void LiquidTaker::on_trade_updated(const MarketData*,
                                   MarketOrderBook*) noexcept {}

void LiquidTaker::on_order_updated(const ExecutionReport*) noexcept {}

void register_liquid_taker_strategy() {
  const static Registrar<LiquidTaker> kReg("taker");
}
}  // namespace trading

// Register LiquidTaker strategy with function pointer dispatch