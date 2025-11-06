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

#ifndef MARKET_CONSUMER_H
#define MARKET_CONSUMER_H

#include <string>

#include "gateway/market_data_gateway_interface.h"
#include "logger.h"

namespace trading {

/**
 * @brief Market consumer that delegates to a pluggable IMarketDataGateway implementation
 *
 * This class acts as a facade that injects a market data gateway implementation
 * (FIX, WebSocket, or Test) for market data subscription, enabling runtime selection
 * and testability.
 */
class MarketConsumer {
 public:
  MarketConsumer(common::Logger* logger, IMarketDataGateway* gateway,
                 const std::string& req_id, const std::string& depth,
                 const std::string& symbol);
  ~MarketConsumer();
  void stop();

 private:
  common::Logger::Producer logger_;
  IMarketDataGateway* gateway_;
};
}  // namespace trading

#endif  //MARKET_CONSUMER_H