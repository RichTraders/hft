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

namespace trading {

LiquidTaker::LiquidTaker(OrderManagerT* const order_manager,
    const FeatureEngineT* const feature_engine,
    const common::Logger::Producer& logger,
    const common::TradeEngineCfgHashMap&)
    : Base(order_manager, feature_engine, logger) {}

void LiquidTaker::on_orderbook_updated(const common::TickerId&, common::Price,
    common::Side, const MarketOrderBookT*) const noexcept {}

void LiquidTaker::on_trade_updated(const MarketData*,
    MarketOrderBookT*) const noexcept {}

void LiquidTaker::on_order_updated(const ExecutionReport*) noexcept {}

}  // namespace trading
