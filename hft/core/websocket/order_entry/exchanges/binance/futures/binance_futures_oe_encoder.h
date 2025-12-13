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

#ifndef BINANCE_FUTURES_OE_ENCODER_H
#define BINANCE_FUTURES_OE_ENCODER_H

#include "common/logger.h"
#include "order_entry.h"

namespace core {

class BinanceFuturesOeEncoder {
 public:
  explicit BinanceFuturesOeEncoder(const common::Logger::Producer& logger)
      : logger_(logger) {}

  [[nodiscard]] std::string create_log_on_message(const std::string& signature,
      const std::string& timestamp) const;
  [[nodiscard]] std::string create_log_out_message() const;
  [[nodiscard]] std::string create_heartbeat_message() const;

  [[nodiscard]] std::string create_user_data_stream_subscribe() const;
  [[nodiscard]] std::string create_user_data_stream_unsubscribe() const;
  [[nodiscard]] std::string create_user_data_stream_ping() const;
  [[nodiscard]] std::string create_order_message(
      const trading::NewSingleOrderData& order) const;
  [[nodiscard]] std::string create_cancel_order_message(
      const trading::OrderCancelRequest& cancel) const;
  [[nodiscard]] std::string create_cancel_and_reorder_message(
      const trading::OrderCancelAndNewOrderSingle& replace) const;

 private:
  const common::Logger::Producer& logger_;
};

}  // namespace core

#include "binance_futures_oe_encoder.tpp"

#endif  //BINANCE_FUTURES_OE_ENCODER_H
