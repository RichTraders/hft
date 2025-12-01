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

template <typename App>
LiquidTakerTemplate<App>::LiquidTakerTemplate(
    OrderManagerT* const order_manager,
    const FeatureEngineT* const feature_engine, common::Logger* logger,
    const common::TradeEngineCfgHashMap&)
    : Base(order_manager, feature_engine, logger) {}

template <typename App>
void LiquidTakerTemplate<App>::on_orderbook_updated(const common::TickerId&,
    common::Price, common::Side, const MarketOrderBookT*) const noexcept {}

template <typename App>
void LiquidTakerTemplate<App>::on_trade_updated(const MarketData*,
    MarketOrderBookT*) const noexcept {}

template <typename App>
void LiquidTakerTemplate<App>::on_order_updated(
    const ExecutionReport*) noexcept {}

#ifdef ENABLE_WEBSOCKET
template class LiquidTakerTemplate<core::WsOrderEntryApp>;
#else
template class LiquidTakerTemplate<core::FixOrderEntryApp>;
#endif

}  // namespace trading
