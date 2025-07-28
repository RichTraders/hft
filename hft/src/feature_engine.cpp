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

#include "feature_engine.h"
#include "common/logger.h"
#include "core/NewOroFix44/market_data.h"
#include "order_book.h"

using common::Price;
using common::Side;
using common::TickerId;

namespace trading {
void FeatureEngine::on_trade_updated(const MarketData* market_update,
                                     trading::MarketOrderBook* book) noexcept {
  const auto* bbo = book->get_bbo();
  if (LIKELY(bbo->bid_price.value != common::kPriceInvalid &&
             bbo->ask_price.value != common::kPriceInvalid)) {
    agg_trade_qty_ratio_ =
        static_cast<double>(market_update->qty.value) /
        (market_update->side == Side::kBuy ? bbo->ask_qty.value
                                           : bbo->bid_qty.value);
  }

  logger_->info(std::format("{} mkt-price:{} agg-trade-ratio:{}",
                            market_update->toString(), mkt_price_,
                            agg_trade_qty_ratio_));
}

void FeatureEngine::on_order_book_updated(
    const Price price, const Side side,
    trading::MarketOrderBook* book) noexcept {
  const auto* bbo = book->get_bbo();
  if (LIKELY(bbo->bid_price != common::kPriceInvalid &&
             bbo->ask_price != common::kPriceInvalid)) {
    mkt_price_ = (bbo->bid_price.value * bbo->ask_qty.value +
                  bbo->ask_price.value * bbo->bid_qty.value) /
                 static_cast<double>(bbo->bid_qty.value + bbo->ask_qty.value);
  }

  logger_->info(std::format("price:{} side:{} mkt-price:{} agg-trade-ratio:{}",
                            common::toString(price), common::toString(side),
                            mkt_price_, agg_trade_qty_ratio_));
}
}  // namespace trading