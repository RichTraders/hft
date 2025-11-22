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

#ifndef MOMENTUM_STRATEGY_H
#define MOMENTUM_STRATEGY_H
#include "base_strategy.hpp"

struct MarketData;

namespace trading {
class ObiVwapMomentumStrategy : public BaseStrategy<ObiVwapMomentumStrategy> {
 public:
  ObiVwapMomentumStrategy(OrderManager<ObiVwapMomentumStrategy>* order_manager,
              const FeatureEngine<ObiVwapMomentumStrategy>* feature_engine,
              common::Logger* logger,
              const common::TradeEngineCfgHashMap& ticker_cfg);
  void on_orderbook_updated(
      const common::TickerId& ticker, common::Price, common::Side,
      const MarketOrderBook<ObiVwapMomentumStrategy>* order_book) noexcept;

  void on_trade_updated(const MarketData*,
                        MarketOrderBook<ObiVwapMomentumStrategy>*) noexcept;

  void on_order_updated(const ExecutionReport*) noexcept;

 private:
  static constexpr int kDefaultOBILevel10 = 10;
  static constexpr int kSafetyMargin = 20;
  const double variance_denominator_;
  const double position_variance_;
  const double enter_threshold_;
  const double exit_threshold_;
  const int obi_level_;
  std::vector<double> bid_qty_;
  std::vector<double> ask_qty_;
};
}  // namespace trading

#endif  //MOMENTUM_STRATEGY_H
