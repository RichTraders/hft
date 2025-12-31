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

#ifndef BINANCE_SPOT_TRAITS_H
#define BINANCE_SPOT_TRAITS_H

#include <string>
#include <string_view>
#include <variant>

#include "binance_md_connection_handler.h"
#include "binance_sbe_ops.h"
#include "binance_spot_dispatcher.h"
#include "binance_spot_domain_converter.h"
#include "binance_spot_encoder.h"
#include "binance_spot_formatter.h"
#include "common/ini_config.hpp"
#include "core/websocket/market_data/exchange_traits.h"
#include "core/websocket/market_data/json_binance_spot_md_decoder.hpp"
#include "schema/spot/response/api_response.h"
#include "schema/spot/response/depth_stream.h"
#include "schema/spot/response/exchange_info_response.h"
#include "schema/spot/response/snapshot.h"
#include "schema/spot/response/trade.h"
#include "schema/spot/sbe/best_bid_ask_sbe.h"
#include "schema/spot/sbe/depth_stream_sbe.h"
#include "schema/spot/sbe/snapshot_sbe.h"
#include "schema/spot/sbe/trade_sbe.h"

struct BinanceSpotTraits {
  using ConnectionHandler = BinanceMdConnectionHandler;
  using SbeOps = BinanceSbeOps;
  using Formatter = BinanceSpotFormatter;
  using Encoder = BinanceSpotEncoder;
  using MdDomainConverter = BinanceSpotMdMessageConverter;
  using DispatchRouter = BinanceDispatchRouter;
  using Decoder = core::JsonBinanceSpotMdDecoder;

  using DepthResponse = schema::DepthResponse;
  using DepthSnapshot = schema::DepthSnapshot;
  using TradeEvent = schema::TradeEvent;
  using SbeDepthResponse = schema::sbe::SbeDepthResponse;
  using SbeTradeEvent = schema::sbe::SbeTradeEvent;
  using SbeDepthSnapshot = schema::sbe::SbeDepthSnapshot;
  using SbeBestBidAsk = schema::sbe::SbeBestBidAsk;
  using ApiResponse = schema::ApiResponse;
  using ExchangeInfoResponse = schema::ExchangeInfoResponse;
  using ModifyOrderResponse = std::monostate;

  static constexpr bool uses_http_exchange_info() { return false; }

  using WireMessage = std::variant<std::monostate, DepthResponse, DepthSnapshot,
      TradeEvent, ApiResponse, ExchangeInfoResponse>;

  static constexpr std::string_view exchange_name() { return "Binance"; }
  static constexpr std::string_view market_type() { return "Spot"; }

  static std::string get_api_host() {
    return INI_CONFIG.get("exchange", "md_api_host", "ws-api.binance.com");
  }

  static std::string get_stream_host() {
    return INI_CONFIG.get("exchange", "md_stream_host", "stream.binance.com");
  }

  static std::string get_api_endpoint_path() {
    return INI_CONFIG.get("exchange",
        "md_api_endpoint_path",
        "/ws-api/v3?returnRateLimits=false");
  }

  static std::string get_stream_endpoint_path() {
    return INI_CONFIG.get("exchange",
        "md_ws_path",
        "/stream?streams=btcusdt@depth@100ms/btcusdt@trade");
  }

  static constexpr int kDefaultPort = 9443;

  static int get_api_port() {
    return INI_CONFIG.get_int("exchange", "md_port", kDefaultPort);
  }

  static int get_stream_port() {
    return INI_CONFIG.get_int("exchange", "md_port", kDefaultPort);
  }

  static bool use_ssl() {
    return INI_CONFIG.get_int("exchange", "md_use_ssl", 1) != 0;
  }

  static constexpr bool supports_json() { return true; }
  static constexpr bool supports_sbe() { return true; }
};

static_assert(ExchangeTraits<BinanceSpotTraits>,
    "BinanceSpotTraits must satisfy ExchangeTraits concept");

#endif  //BINANCE_SPOT_TRAITS_H
