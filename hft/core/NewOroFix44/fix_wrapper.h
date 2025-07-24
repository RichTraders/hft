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

#ifndef FIX_WRAPPER_H
#define FIX_WRAPPER_H

#include "logger.h"
#include "market_data.h"
#include "memory_pool.hpp"

namespace FIX8 {
class Message;
}

namespace core {
class Fix {
public:
  using SendId = std::string;
  using TargetId = std::string;

  //DEPTH_STREAM, BOOK_TICKER_STREAM, TRADE_STREAM
  using RequestId = std::string;
  using MarketDepthLevel = std::string;
  using SymbolId = std::string;

  Fix(SendId sender_comp_id,
      TargetId target_comp_id,
      common::Logger* logger,
      common::MemoryPool<MarketData>* pool);

  std::string create_log_on_message(
      const std::string& sig_b64, const std::string& timestamp);

  std::string create_log_out_message();
  std::string create_heartbeat_message(FIX8::Message* message);

  std::string create_market_data_subscription_message(
      const RequestId& request_id,
      const MarketDepthLevel& level, const SymbolId& symbol);
  std::string create_trade_data_subscription_message(
      const RequestId& request_id, const MarketDepthLevel& level,
      const SymbolId& symbol);
  MarketUpdateData create_market_data(FIX8::Message* msg) const;
  MarketUpdateData create_snapshot_data_message(FIX8::Message* msg) const;

  FIX8::Message* decode(const std::string& message);
  [[nodiscard]] const std::string get_signature_base64(
      const std::string& timestamp) const;
  static std::string timestamp();

  static void encode(std::string& data, FIX8::Message* msg);

private:
  int64_t sequence_{1};
  common::Logger* logger_;
  const std::string sender_comp_id_;
  const std::string target_comp_id_;
  common::MemoryPool<MarketData>* market_data_pool_;
};
}


#endif //FIX_WRAPPER_H