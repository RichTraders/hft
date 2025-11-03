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
class MarketOrderBook;
class FeatureEngine;
class OrderManager;
struct ExecutionReport;

class BaseStrategy {
 public:
  BaseStrategy(OrderManager* const order_manager,
               const FeatureEngine* const feature_engine,
               common::Logger* logger)
      : order_manager_(order_manager),
        feature_engine_(feature_engine),
        logger_(logger->make_producer()) {}

  virtual ~BaseStrategy() = default;

 protected:
  OrderManager* order_manager_;
  const FeatureEngine* feature_engine_;
  common::Logger::Producer logger_;
};
}  // namespace trading

#endif  //BASE_STRATEGY_H