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
#include "base_strategy.hpp"

struct MarketData;

namespace trading {
template <typename Strategy, typename OeTraits>
class FeatureEngine;
template <typename Strategy, typename OeTraits>
class MarketOrderBook;

template <typename OeTraits>
class LiquidTaker : public BaseStrategy<LiquidTaker<OeTraits>, OeTraits> {
 public:
  using QuoteIntentType = std::conditional_t<OeTraits::supports_position_side(),
      FuturesQuoteIntent, SpotQuoteIntent>;
  using OrderManagerT = OrderManager<LiquidTaker<OeTraits>, OeTraits>;
  using FeatureEngineT = FeatureEngine<LiquidTaker<OeTraits>, OeTraits>;
  using MarketOrderBookT = MarketOrderBook<LiquidTaker<OeTraits>, OeTraits>;

  LiquidTaker(OrderManagerT* order_manager,
      const FeatureEngineT* feature_engine,
      const common::Logger::Producer& logger,
      const common::TradeEngineCfgHashMap&)
      : BaseStrategy<LiquidTaker<OeTraits>, OeTraits>(order_manager,
            feature_engine, logger) {}

  void on_orderbook_updated(const common::TickerId&, common::Price,
      common::Side, const MarketOrderBookT*) const noexcept {}

  void on_trade_updated(const MarketData*, MarketOrderBookT*) const noexcept {}

  void on_order_updated(const ExecutionReport*) noexcept {}
};
}  // namespace trading

#endif  //LIQUID_TAKER_H
