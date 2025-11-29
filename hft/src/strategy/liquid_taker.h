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

namespace core {
class FixOrderEntryApp;
#ifdef ENABLE_WEBSOCKET
class WsOrderEntryApp;
#endif
}  // namespace core

namespace trading {
template <typename Strategy, typename App>
class FeatureEngine;
template <typename Strategy, typename App>
class MarketOrderBook;

template <typename App>
class LiquidTakerTemplate : public BaseStrategy<LiquidTakerTemplate<App>, App> {
 public:
  using Base = BaseStrategy<LiquidTakerTemplate<App>, App>;
  using OrderManagerT = OrderManager<LiquidTakerTemplate<App>, App>;
  using FeatureEngineT = FeatureEngine<LiquidTakerTemplate<App>, App>;
  using MarketOrderBookT = MarketOrderBook<LiquidTakerTemplate<App>, App>;

  LiquidTakerTemplate(OrderManagerT* order_manager,
      const FeatureEngineT* feature_engine, common::Logger* logger,
      const common::TradeEngineCfgHashMap&);

  void on_orderbook_updated(const common::TickerId&, common::Price,
      common::Side, const MarketOrderBookT*) const noexcept;

  void on_trade_updated(const MarketData*, MarketOrderBookT*) const noexcept;

  void on_order_updated(const ExecutionReport*) noexcept;
};

using LiquidTakerFix = LiquidTakerTemplate<core::FixOrderEntryApp>;
#ifdef ENABLE_WEBSOCKET
using LiquidTakerWs = LiquidTakerTemplate<core::WsOrderEntryApp>;
using LiquidTaker = LiquidTakerWs;
#else
using LiquidTaker = LiquidTakerFix;
#endif

}  // namespace trading

#endif  //LIQUID_TAKER_H
