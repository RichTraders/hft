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

#ifndef WS_MD_MAPPER_H
#define WS_MD_MAPPER_H

#include <variant>

#include "common/logger.h"
#include "ws_md_wire_message.h"

#include "common/memory_pool.hpp"
#include "core/market_data.h"
#include "schema/response/depth_stream.h"
#include "schema/response/exchange_info_response.h"
#include "schema/response/snapshot.h"
#include "schema/response/trade.h"

namespace core {

class WsMdDomainMapper {
 public:
  using WireMessage = WsMdWireMessage;

  WsMdDomainMapper(const common::Logger::Producer& logger,
      common::MemoryPool<MarketData>* pool)
      : logger_(logger), market_data_pool_(pool) {}

  [[nodiscard]] MarketUpdateData to_market_data(const WireMessage& msg) const;
  [[nodiscard]] MarketUpdateData to_snapshot_data(const WireMessage& msg) const;
  [[nodiscard]] InstrumentInfo to_instrument_info(const WireMessage& msg) const;
  [[nodiscard]] MarketDataReject to_reject(const WireMessage& msg) const;

 private:
  using Side = common::Side;
  using MarketUpdateType = common::MarketUpdateType;

  [[nodiscard]] MarketData* make_entry(const std::string& symbol, Side side,
      double price, double qty, MarketUpdateType update_type) const;

  [[nodiscard]] const schema::DepthResponse* as_depth(
      const WireMessage& msg) const;
  [[nodiscard]] const schema::DepthSnapshot* as_depth_snapshot(
      const WireMessage& msg) const;
  [[nodiscard]] const schema::TradeEvent* as_trade(
      const WireMessage& msg) const;
  [[nodiscard]] const schema::ExchangeInfoResponse* as_exchange_info(
      const WireMessage& msg) const;
  [[nodiscard]] const schema::ApiResponse* as_api_response(
      const WireMessage& msg) const;

  [[nodiscard]] MarketUpdateData build_depth_update(
      const schema::DepthResponse& msg, MarketDataType type) const;
  [[nodiscard]] MarketUpdateData build_depth_snapshot(
      const schema::DepthSnapshot& msg, MarketDataType type) const;
  [[nodiscard]] MarketUpdateData build_trade_update(
      const schema::TradeEvent& msg) const;

  const common::Logger::Producer& logger_;
  common::MemoryPool<MarketData>* market_data_pool_;
};

}  // namespace core

#endif  //WS_MD_MAPPER_H
