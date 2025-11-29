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

#include "common/memory_pool.hpp"
#include "core/market_data.h"
#include "logger.h"

namespace FIX8 {
class Message;
class GroupBase;
}  // namespace FIX8

namespace core {
class FixMdCore {
 public:
  using WireMessage = FIX8::Message*;
  using SendId = std::string;
  using TargetId = std::string;

  //DEPTH_STREAM, BOOK_TICKER_STREAM, TRADE_STREAM
  using RequestId = std::string;
  using MarketDepthLevel = std::string;
  using SymbolId = std::string;

  FixMdCore(SendId sender_comp_id, TargetId target_comp_id,
      common::Logger* logger, common::MemoryPool<MarketData>* pool);
  ~FixMdCore();

  std::string create_log_on_message(const std::string& sig_b64,
      const std::string& timestamp);

  std::string create_log_out_message();
  std::string create_heartbeat_message(FIX8::Message* message);

  std::string create_market_data_subscription_message(
      const RequestId& request_id, const MarketDepthLevel& level,
      const SymbolId& symbol, bool subscribe);
  std::string create_trade_data_subscription_message(
      const RequestId& request_id, const MarketDepthLevel& level,
      const SymbolId& symbol);
  MarketUpdateData create_market_data_message(FIX8::Message* msg);
  MarketUpdateData create_snapshot_data_message(FIX8::Message* msg);
  std::string create_instrument_list_request_message(
      const std::string& symbol = "");
  InstrumentInfo create_instrument_list_message(FIX8::Message* msg);
  MarketDataReject create_reject_message(FIX8::Message* msg);

  FIX8::Message* decode(const std::string& message);
  std::string encode(FIX8::Message* message, int reserve = 0);

 private:
  int64_t sequence_{1};
  common::Logger::Producer logger_;
  const std::string sender_comp_id_;
  const std::string target_comp_id_;
  common::MemoryPool<MarketData>* market_data_pool_;

  MarketUpdateData _create_market_data_message(const FIX8::GroupBase* msg);
  MarketUpdateData _create_trade_data_message(const FIX8::GroupBase* msg);

  // Helper methods to reduce code duplication
  template <typename MessageType>
  void populate_standard_header(MessageType& request);

  template <typename MessageType>
  void populate_standard_header(MessageType& request,
      const std::string& timestamp);

  MarketData* allocate_with_retry(common::MarketUpdateType type,
      const std::string& symbol, char side, double price, const void* qty,
      const char* context);

  template <typename RequestType>
  void add_md_entry_types(RequestType& request, const std::vector<char>& types);

  template <typename RequestType>
  void add_symbol_group(RequestType& request, const std::string& symbol);
};
}  // namespace core

#endif  //FIX_MD_CORE_H
