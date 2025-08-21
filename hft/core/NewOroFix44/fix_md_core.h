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

#ifndef FIX_MD_CORE_H
#define FIX_MD_CORE_H

#include "authorization.h"
#include "logger.h"
#include "market_data.h"
#include "memory_pool.hpp"

namespace FIX8 {
class Message;
}

namespace core {
class FixMdCore {
public:
  using SendId = std::string;
  using TargetId = std::string;

  //DEPTH_STREAM, BOOK_TICKER_STREAM, TRADE_STREAM
  using RequestId = std::string;
  using MarketDepthLevel = std::string;
  using SymbolId = std::string;

  FixMdCore(SendId sender_comp_id,
            TargetId target_comp_id,
            common::Logger* logger,
            common::MemoryPool<MarketData>* pool,
            const Authorization& authorization);
  ~FixMdCore();

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
  MarketUpdateData create_market_data_message(FIX8::Message* msg);
  MarketUpdateData create_snapshot_data_message(FIX8::Message* msg);

  FIX8::Message* decode(const std::string& message);
private:
  int64_t sequence_{1};
  common::Logger* logger_;
  const std::string sender_comp_id_;
  const std::string target_comp_id_;
  common::MemoryPool<MarketData>* market_data_pool_;
  Authorization authorization_;
};
}


#endif //FIX_MD_CORE_H