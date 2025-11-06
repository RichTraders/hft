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

namespace trading {
struct RequestCommon;
}

namespace trading {

/**
 * @brief Gateway interface for order execution
 *
 * This interface abstracts the underlying order execution mechanism,
 * allowing different implementations (FIX, WebSocket, REST, Mock, etc.)
 * to be used interchangeably for production and testing.
 */
class IGateway {
 public:
  virtual ~IGateway() = default;

  /**
   * @brief Send an order request to the execution venue
   * @param request The order request to send
   */
  virtual void send_order(const RequestCommon& request) = 0;

  /**
   * @brief Stop the gateway and close all connections
   */
  virtual void stop() = 0;
};

}  // namespace trading
