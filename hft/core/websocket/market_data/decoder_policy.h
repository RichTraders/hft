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

#ifndef WS_MD_DECODER_POLICY_H
#define WS_MD_DECODER_POLICY_H

#include "common/logger.h"
#include "schema/response/api_response.h"
#include "schema/response/depth_stream.h"
#include "schema/response/exchange_info_response.h"
#include "schema/response/snapshot.h"
#include "schema/response/trade.h"
#include "schema/sbe/best_bid_ask_sbe.h"
#include "schema/sbe/depth_stream_sbe.h"
#include "schema/sbe/snapshot_sbe.h"
#include "schema/sbe/trade_sbe.h"

namespace core {

template <typename T>
concept DecoderPolicy = requires {
  typename T::WireMessage;
  { T::requires_api_key() } -> std::same_as<bool>;
  {
    T::decode(std::declval<std::string_view>(),
        std::declval<const common::Logger::Producer&>())
  } -> std::same_as<typename T::WireMessage>;
};

struct JsonDecoderPolicy {
  using WireMessage = std::variant<std::monostate, schema::DepthResponse,
      schema::DepthSnapshot, schema::TradeEvent, schema::ExchangeInfoResponse,
      schema::ApiResponse>;
  static constexpr bool requires_api_key() { return false; }
  static WireMessage decode(std::string_view payload,
      const common::Logger::Producer& logger);
};

struct SbeDecoderPolicy {
  using WireMessage =
      std::variant<std::monostate, schema::sbe::SbeDepthResponse,
          schema::sbe::SbeDepthSnapshot, schema::DepthSnapshot,
          schema::sbe::SbeTradeEvent, schema::sbe::SbeBestBidAsk,
          schema::ExchangeInfoResponse, schema::ApiResponse>;
  static constexpr bool requires_api_key() { return true; }
  static WireMessage decode(std::string_view payload,
      const common::Logger::Producer& logger);
};

}  // namespace core

#endif  // WS_MD_DECODER_POLICY_H
