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

#include "feature_engine.hpp"
#include "market_consumer.hpp"
#include "order_book.hpp"
#include "order_gateway.hpp"
#include "order_manager.hpp"
#include "trade_engine.hpp"

namespace trading {
template <typename Strategy>
class MarketOrderBook;
template <typename Strategy>
class FeatureEngine;
template <typename Strategy>
class OrderManager;
struct ExecutionReport;

template <typename Strategy>
class BaseStrategy {
 public:
  BaseStrategy(OrderManager<Strategy>* const order_manager,
      const FeatureEngine<Strategy>* const feature_engine,
      const common::Logger::Producer& logger)
      : order_manager_(order_manager),
        feature_engine_(feature_engine),
        logger_(logger) {}

  ~BaseStrategy() = default;

 protected:
  OrderManager<Strategy>* order_manager_;
  const FeatureEngine<Strategy>* feature_engine_;
  const common::Logger::Producer& logger_;
};
}  // namespace trading

#endif  //BASE_STRATEGY_H
