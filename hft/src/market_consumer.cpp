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

#include "market_consumer.h"

namespace trading {

MarketConsumer::MarketConsumer(common::Logger* logger,
                               IMarketDataGateway* gateway,
                               const std::string& req_id,
                               const std::string& depth,
                               const std::string& symbol)
    : logger_(logger->make_producer()), gateway_(gateway) {
  // Subscribe to market data
  gateway_->subscribe_market_data(req_id, depth, symbol, /*subscribe=*/true);
  gateway_->request_instrument_list(symbol);

  logger_.info("[Constructor] MarketConsumer Created");
}

MarketConsumer::~MarketConsumer() {
  logger_.info("[Destructor] MarketConsumer Destroy");
}

void MarketConsumer::stop() {
  gateway_->stop();
}

}  // namespace trading
