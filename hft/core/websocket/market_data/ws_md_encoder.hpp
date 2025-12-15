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

#ifndef WS_MD_ENCODER_H
#define WS_MD_ENCODER_H

#include "common/logger.h"
#include "exchange_traits.h"

namespace core {
template<ExchangeTraits Exchange>
class WsMdEncoder {
 public:
  using RequestId = std::string_view;
  using MarketDepthLevel = std::string_view;
  using SymbolId = std::string_view;
  explicit WsMdEncoder(const common::Logger::Producer& logger)
      : logger_(logger) {}

  [[nodiscard]] std::string create_market_data_subscription_message(
      const RequestId& request_id, const MarketDepthLevel& level,
      const SymbolId& symbol, bool subscribe) const {
    return encoder_.create_market_data_subscription_message(request_id,level,symbol,subscribe);
  }
  [[nodiscard]] std::string create_snapshot_data_subscription_message(
      const MarketDepthLevel& level, const SymbolId& symbol) const {
    return encoder_.create_snapshot_data_subscription_message(level,symbol);
  }
  [[nodiscard]] std::string create_trade_data_subscription_message(
      const RequestId& request_id, const MarketDepthLevel& level,
      const SymbolId& symbol, bool subscribe) const {
    return encoder_.create_trade_data_subscription_message(request_id,level,symbol,subscribe);
  }
  [[nodiscard]] std::string request_instrument_list_message(
      const std::string& symbol) const {
    return encoder_.request_instrument_list_message(symbol);
  }

 private:
  const common::Logger::Producer& logger_;
  mutable typename Exchange::Encoder encoder_;
};
}  // namespace core
#endif  //WS_MD_ENCODER_H
