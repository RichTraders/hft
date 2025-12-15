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

#ifndef BASE_STRATEGY_H
#define BASE_STRATEGY_H

#include "common/logger.h"

#include "feature_engine.tpp"
#include "market_consumer.tpp"
#include "order_book.tpp"
#include "order_gateway.tpp"
#include "order_manager.tpp"
#include "trade_engine.tpp"

namespace trading {
template <typename Strategy, typename OeTraits>
class MarketOrderBook;
template <typename Strategy, typename OeTraits>
class FeatureEngine;
template <typename Strategy, typename OeTraits>
class OrderManager;
struct ExecutionReport;

template <typename Strategy, typename OeTraits>
class BaseStrategy {
 public:
  BaseStrategy(OrderManager<Strategy, OeTraits>* const order_manager,
      const FeatureEngine<Strategy, OeTraits>* const feature_engine,
      const common::Logger::Producer& logger)
      : order_manager_(order_manager),
        feature_engine_(feature_engine),
        logger_(logger) {}

  ~BaseStrategy() = default;

 protected:
  OrderManager<Strategy, OeTraits>* order_manager_;
  const FeatureEngine<Strategy, OeTraits>* feature_engine_;
  const common::Logger::Producer& logger_;
};
}  // namespace trading

#endif  //BASE_STRATEGY_H
