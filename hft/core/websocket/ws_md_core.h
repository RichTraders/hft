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

#ifndef WS_MD_CORE_H
#define WS_MD_CORE_H

#include "common/logger.h"
#include "common/memory_pool.hpp"
#include "core/market_data.h"
#include "core/websocket/schema/depth_stream.h"
#include "core/websocket/schema/trade.h"
#include "schema/response/exchange_info_response.h"
#include "schema/response/snapshot.h"

namespace core {

class WsMdCore {
 public:
  using WireMessage = std::variant<std::monostate, schema::DepthResponse,
      schema::TradeEvent, schema::DepthSnapshot, schema::ExchangeInfoResponse>;
  using RequestId = std::string;
  using MarketDepthLevel = std::string;
  using SymbolId = std::string;

  WsMdCore(common::Logger* logger, common::MemoryPool<MarketData>* pool);

  [[nodiscard]] std::string create_market_data_subscription_message(
      const RequestId& request_id, const MarketDepthLevel& level,
      const SymbolId& symbol, bool subscribe);
  [[nodiscard]] std::string create_trade_data_subscription_message(
      const RequestId& request_id, const MarketDepthLevel& level,
      const SymbolId& symbol) const;
  [[nodiscard]] MarketUpdateData create_market_data_message(
      const WireMessage& msg) const;
  [[nodiscard]] MarketUpdateData create_snapshot_data_message(
      const WireMessage& msg) const;
  [[nodiscard]] std::string request_instrument_list_message(
      const std::string& symbol) const;
  [[nodiscard]] InstrumentInfo create_instrument_list_message(
      const WireMessage& msg) const;
  [[nodiscard]] MarketDataReject create_reject_message(
      const WireMessage& msg) const;
  [[nodiscard]] WireMessage decode(std::string_view payload) const;

 private:
  MarketUpdateData build_depth_update(const schema::DepthResponse& msg,
      MarketDataType type) const;
  MarketUpdateData build_depth_snapshot(const schema::DepthSnapshot& msg,
      MarketDataType type) const;
  MarketUpdateData build_trade_update(const schema::TradeEvent& msg) const;
  MarketData* make_entry(const std::string& symbol, common::Side side,
      double price, double qty, common::MarketUpdateType update_type) const;

  [[nodiscard]] static std::string extract_symbol(const WireMessage& msg);
  template <class T>
  WireMessage decode_or_log(std::string_view payload,
      std::string_view label) const;

  common::Logger::Producer logger_;
  common::MemoryPool<MarketData>* market_data_pool_;
  mutable int request_sequence_{1};
};

}  // namespace core

#endif
