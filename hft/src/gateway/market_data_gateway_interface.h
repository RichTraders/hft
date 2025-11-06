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

#pragma once

#include <string>

namespace trading {

/**
 * @brief Gateway interface for market data subscription
 *
 * This interface abstracts the underlying market data protocol,
 * allowing different implementations (FIX, WebSocket, Mock, etc.)
 * to be used interchangeably for production and testing.
 */
class IMarketDataGateway {
 public:
  virtual ~IMarketDataGateway() = default;

  /**
   * @brief Subscribe to market data for a symbol
   * @param req_id Request ID (e.g., "DEPTH_STREAM")
   * @param depth Orderbook depth level
   * @param symbol Trading symbol (e.g., "BTCUSDT")
   * @param subscribe True to subscribe, false to unsubscribe
   */
  virtual void subscribe_market_data(const std::string& req_id,
                                     const std::string& depth,
                                     const std::string& symbol,
                                     bool subscribe) = 0;

  /**
   * @brief Request instrument list for a symbol
   * @param symbol Trading symbol
   */
  virtual void request_instrument_list(const std::string& symbol) = 0;

  /**
   * @brief Stop the gateway and close all connections
   */
  virtual void stop() = 0;
};

}  // namespace trading
