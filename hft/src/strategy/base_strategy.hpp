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

#include "logger.h"
#include "types.h"

namespace trading {
template <class Strategy>
class MarketOrderBook;
template <class Strategy>
class FeatureEngine;
template <class Strategy>
class OrderManager;
struct ExecutionReport;

template <class Strategy>
class BaseStrategy {
 public:
  BaseStrategy(OrderManager<Strategy>* const order_manager,
               const FeatureEngine<Strategy>* const feature_engine,
               common::Logger* logger)
      : order_manager_(order_manager),
        feature_engine_(feature_engine),
        logger_(logger->make_producer()) {}

  ~BaseStrategy() = default;

 protected:
  OrderManager<Strategy>* order_manager_;
  const FeatureEngine<Strategy>* feature_engine_;
  common::Logger::Producer logger_;
};
}  // namespace trading

#include "feature_engine.tpp"
#include "market_consumer.tpp"
#include "order_book.tpp"
#include "order_gateway.tpp"
#include "order_manager.tpp"
#include "trade_engine.tpp"

#endif  //BASE_STRATEGY_H