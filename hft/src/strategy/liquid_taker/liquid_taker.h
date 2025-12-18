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
#include "../base_strategy.hpp"
#include "oe_traits_config.hpp"

struct MarketData;

namespace trading {
template <typename Strategy>
class FeatureEngine;
template <typename Strategy>
class MarketOrderBook;

class LiquidTaker : public BaseStrategy<LiquidTaker> {
 public:
  using QuoteIntentType =
      std::conditional_t<SelectedOeTraits::supports_position_side(),
          FuturesQuoteIntent, SpotQuoteIntent>;
  using OrderManagerT = OrderManager<LiquidTaker>;
  using FeatureEngineT = FeatureEngine<LiquidTaker>;
  using MarketOrderBookT = MarketOrderBook<LiquidTaker>;

  LiquidTaker(OrderManagerT* order_manager,
      const FeatureEngineT* feature_engine,
      const common::Logger::Producer& logger,
      const common::TradeEngineCfgHashMap&)
      : BaseStrategy<LiquidTaker>(order_manager, feature_engine, logger) {}

  void on_orderbook_updated(const common::TickerId&, common::Price,
      common::Side, const MarketOrderBookT*) const noexcept {}

  void on_trade_updated(const MarketData*, MarketOrderBookT*) const noexcept {}

  void on_order_updated(const ExecutionReport*) noexcept {}
};
}  // namespace trading

#endif  //LIQUID_TAKER_H
