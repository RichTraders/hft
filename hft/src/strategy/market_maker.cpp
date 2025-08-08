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
 
 #include "market_maker.h"

void trading::MarketMaker::on_orderbook_updated(common::TickerId, common::Price,
    common::Side, const MarketOrderBook*) noexcept {}

void trading::MarketMaker::on_trade_updated(const MarketData*,
    MarketOrderBook*) noexcept {

}

void trading::MarketMaker::on_order_updated(const ExecutionReport*) noexcept {}