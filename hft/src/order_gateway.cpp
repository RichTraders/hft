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

#include "order_gateway.h"

#include "trade_engine.h"

namespace trading {

OrderGateway::OrderGateway(common::Logger* logger, IGateway* gateway)
    : logger_(logger->make_producer()),
      trade_engine_(nullptr),
      gateway_(gateway) {
  logger_.info("[Constructor] OrderGateway Constructor");
}

OrderGateway::~OrderGateway() {
  logger_.info("[Destructor] OrderGateway Destroy");
}

void OrderGateway::stop() const {
  gateway_->stop();
}

void OrderGateway::init_trade_engine(TradeEngine* trade_engine) {
  trade_engine_ = trade_engine;
}

void OrderGateway::order_request(const RequestCommon& request) {
  gateway_->send_order(request);
}

}  // namespace trading