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

#include <type_traits>
#include <variant>

#include "common/logger.h"
#include "common/memory_pool.hpp"
#include "core/market_data.h"
#include "exchange_traits.h"
#include "protocol_decoder.h"

namespace core {

template <ExchangeTraits Exchange, ProtocolDecoder Decoder>
class WsMdDomainMapper {
 public:
  using WireMessage = Decoder::WireMessage;
  using DecodeType = Decoder;
  using Converter = typename Exchange::MdDomainConverter;

  WsMdDomainMapper(const common::Logger::Producer& logger,
      common::MemoryPool<MarketData>* pool)
      : logger_(logger), market_data_pool_(pool) {}

  [[nodiscard]] MarketUpdateData to_market_data(const WireMessage& msg) const {
    if constexpr (std::is_same_v<Converter, std::monostate>) {
      (void)msg;
      return MarketUpdateData();  // Not implemented for this exchange
    } else {
      Converter converter(logger_, market_data_pool_);
      return std::visit(converter.make_market_data_visitor(), msg);
    }
  }
  [[nodiscard]] MarketUpdateData to_snapshot_data(
      const WireMessage& msg) const {
    if constexpr (std::is_same_v<Converter, std::monostate>) {
      (void)msg;
      return MarketUpdateData();  // Not implemented for this exchange
    } else {
      Converter converter(logger_, market_data_pool_);
      return std::visit(converter.make_snapshot_visitor(), msg);
    }
  }
  [[nodiscard]] InstrumentInfo to_instrument_info(
      const WireMessage& /*msg*/) const {
    // Converter converter(logger_, market_data_pool_);
    // return std::visit(converter.make_instrument_visitor(), msg);
    return InstrumentInfo();
  }
  [[nodiscard]] MarketDataReject to_reject(const WireMessage& /*msg*/) const {
    // Converter converter(logger_, market_data_pool_);
    // return std::visit(converter.make_reject_visitor(), msg);
    return MarketDataReject();
  }

 private:
  using Side = common::Side;
  using MarketUpdateType = common::MarketUpdateType;

  const common::Logger::Producer& logger_;
  common::MemoryPool<MarketData>* market_data_pool_;
};

}  // namespace core

#endif  //WS_MD_MAPPER_H
