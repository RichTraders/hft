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
namespace core {
class WsMdEncoder {
 public:
  using RequestId = std::string;
  using MarketDepthLevel = std::string;
  using SymbolId = std::string;
  explicit WsMdEncoder(const common::Logger::Producer& logger)
      : logger_(logger) {}

  [[nodiscard]] std::string create_market_data_subscription_message(
      const RequestId& request_id, const MarketDepthLevel& level,
      const SymbolId& symbol, bool subscribe) const;
  [[nodiscard]] std::string create_trade_data_subscription_message(
      const RequestId& request_id, const MarketDepthLevel& level,
      const SymbolId& symbol) const;
  [[nodiscard]] std::string request_instrument_list_message(
      const std::string& symbol) const;

 private:
  const common::Logger::Producer& logger_;
  mutable int request_sequence_{1};
};
}  // namespace core
#endif  //WS_MD_ENCODER_H
