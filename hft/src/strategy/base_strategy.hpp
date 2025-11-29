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

namespace core {
#ifdef ENABLE_WEBSOCKET
class WsOrderEntryApp;
#else
class FixOrderEntryApp;
#endif
}  // namespace core

namespace trading {
template <class Strategy, typename App>
class MarketOrderBook;
template <class Strategy, typename App>
class FeatureEngine;
template <class Strategy, typename App>
class OrderManager;
struct ExecutionReport;

template <class Strategy, typename App>
  requires core::OrderEntryAppLike<App>
class BaseStrategy {
 public:
  BaseStrategy(OrderManager<Strategy, App>* const order_manager,
      const FeatureEngine<Strategy, App>* const feature_engine,
      common::Logger* logger)
      : order_manager_(order_manager),
        feature_engine_(feature_engine),
        logger_(logger->make_producer()) {}

  ~BaseStrategy() = default;

 protected:
  OrderManager<Strategy, App>* order_manager_;
  const FeatureEngine<Strategy, App>* feature_engine_;
  common::Logger::Producer logger_;
};
}  // namespace trading

#endif  //BASE_STRATEGY_H
