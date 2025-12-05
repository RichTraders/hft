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
#include "common/memory_pool.hpp"
#include "core/market_data.h"
#include "decoder_policy.h"

namespace core {

// Policy-based WebSocket Market Data Domain Mapper
// Maps wire messages to domain objects (MarketUpdateData, InstrumentInfo, etc.)
// Uses if constexpr to handle both JSON and SBE message types
template <DecoderPolicy Policy>
class WsMdDomainMapper {
 public:
  using WireMessage = typename Policy::WireMessage;

  WsMdDomainMapper(const common::Logger::Producer& logger,
      common::MemoryPool<MarketData>* pool)
      : logger_(logger), market_data_pool_(pool) {}

  // Implemented in ws_md_domain_mapper.tpp
  [[nodiscard]] MarketUpdateData to_market_data(const WireMessage& msg) const;
  [[nodiscard]] MarketUpdateData to_snapshot_data(const WireMessage& msg) const;
  [[nodiscard]] InstrumentInfo to_instrument_info(const WireMessage& msg) const;
  [[nodiscard]] MarketDataReject to_reject(const WireMessage& msg) const;

 private:
  using Side = common::Side;
  using MarketUpdateType = common::MarketUpdateType;

  // Common helper (Policy-independent)
  [[nodiscard]] MarketData* make_entry(const std::string& symbol, Side side,
      double price, double qty, MarketUpdateType update_type) const;

  // JSON-specific builders (implemented in .tpp)
  [[nodiscard]] MarketUpdateData build_json_depth_update(
      const schema::DepthResponse& msg, MarketDataType type) const;
  [[nodiscard]] MarketUpdateData build_json_depth_snapshot(
      const schema::DepthSnapshot& msg, MarketDataType type) const;
  [[nodiscard]] MarketUpdateData build_json_trade_update(
      const schema::TradeEvent& msg) const;

  // SBE-specific builders (implemented in .tpp)
  [[nodiscard]] MarketUpdateData build_sbe_depth_update(
      const schema::sbe::SbeDepthResponse& msg, MarketDataType type) const;
  [[nodiscard]] MarketUpdateData build_sbe_depth_snapshot(
      const schema::sbe::SbeDepthSnapshot& msg, MarketDataType type) const;
  [[nodiscard]] MarketUpdateData build_sbe_trade_update(
      const schema::sbe::SbeTradeEvent& msg) const;
  [[nodiscard]] MarketUpdateData build_sbe_best_bid_ask(
      const schema::sbe::SbeBestBidAsk& msg) const;

  const common::Logger::Producer& logger_;
  common::MemoryPool<MarketData>* market_data_pool_;
};

}  // namespace core

// Include template implementation
#include "ws_md_domain_mapper.tpp"

#endif  //WS_MD_MAPPER_H
