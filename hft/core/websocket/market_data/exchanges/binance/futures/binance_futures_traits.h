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

#ifndef BINANCE_FUTURES_TRAITS_H
#define BINANCE_FUTURES_TRAITS_H

#include "binance_futures_dispatcher.h"
#include "binance_futures_encoder.h"
#include "binance_futures_formatter.h"
#include "schema/futures/response/api_response.h"
#include "schema/futures/response/depth_stream.h"
#include "schema/futures/response/session_response.h"
#include "schema/futures/response/snapshot.h"
#include "schema/futures/response/trade.h"

struct BinanceFuturesTraits {
  using SbeOps = std::monostate;
  using Formatter = BinanceFuturesFormatter;
  using Encoder = BinanceFuturesEncoder;
  using MdDomainConverter = std::monostate;
  using DispatchRouter = BinanceDispatchRouter;

  using SbeDepthResponse = std::monostate;
  using SbeTradeEvent = std::monostate;
  using SbeDepthSnapshot = std::monostate;
  using SbeBestBidAsk = std::monostate;
  using ExchangeInfoResponse = std::monostate;
  using ModifyOrderResponse = std::monostate;  // Not used in market data

  using DepthResponse = schema::futures::DepthResponse;
  using TradeEvent = schema::futures::AggregateTradeEvent;
  using ApiResponse = schema::futures::ApiResponse;
  using DepthSnapshot = schema::futures::DepthSnapshot;
  using SessionLogOnResponse = std::monostate;  // Not used

  using WireMessage = std::variant<std::monostate, DepthResponse, TradeEvent,
      DepthSnapshot, ApiResponse>;

  static constexpr std::string_view exchange_name() { return "Binance"; }
  static constexpr std::string_view market_type() { return "Futures"; }

  static constexpr std::string_view get_api_host() {
    return "ws-fapi.binance.com";
  }

  static constexpr std::string_view get_stream_host() {
    return "fstream.binance.com";
  }

  static constexpr std::string_view get_api_endpoint_path() {
    return "/ws-fapi/v1?returnRateLimits=false";
  }

  static constexpr std::string_view get_stream_endpoint_path() {
    return "/stream?streams=btcusdt@depth/btcusdt@aggTrade";
  }

  static constexpr int kPort = 443;

  static constexpr int get_api_port() { return kPort; }

  static constexpr int get_stream_port() { return kPort; }

  static constexpr bool use_ssl() { return true; }

  static constexpr bool supports_json() { return true; }
  static constexpr bool supports_sbe() { return false; }

  static bool is_depth_message(std::string_view payload) {
    return payload.contains("@depth");
  }

  static bool is_trade_message(std::string_view payload) {
    return payload.contains("@aggTrade");
  }

  static bool is_snapshot_message(std::string_view payload) {
    return payload.contains("snapshot");
  }
};

#endif  //BINANCE_FUTURES_TRAITS_H
