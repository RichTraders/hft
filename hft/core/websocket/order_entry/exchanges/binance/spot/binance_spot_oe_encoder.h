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

#ifndef BINANCE_SPOT_OE_ENCODER_H
#define BINANCE_SPOT_OE_ENCODER_H

#include <string>

#include "common/fixed_point.hpp"
#include "common/logger.h"
#include "common/precision_config.hpp"
#include "order_entry.h"

namespace core {

class BinanceSpotOeEncoder {
 public:
  explicit BinanceSpotOeEncoder(const common::Logger::Producer& logger)
      : logger_(logger),
        price_precision_(PRECISION_CONFIG.price_precision()),
        qty_precision_(PRECISION_CONFIG.qty_precision()) {}

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
  [[nodiscard]] std::string create_order_all_cancel(
      const trading::OrderMassCancelRequest& request) const;

 private:
  const common::Logger::Producer& logger_;
  int price_precision_;
  int qty_precision_;

  template <typename T>
  std::string to_fixed(T data, int precision) const;
};

}  // namespace core

#include "binance_spot_oe_encoder.tpp"

#endif  //BINANCE_SPOT_OE_ENCODER_H
