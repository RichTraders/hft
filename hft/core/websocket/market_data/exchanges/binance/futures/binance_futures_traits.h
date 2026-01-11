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

#include <string>
#include <string_view>
#include <variant>

#include "binance_future_domain_converter.hpp"
#include "binance_futures_dispatcher.h"
#include "binance_futures_encoder.hpp"
#include "binance_futures_formatter.h"
#include "binance_futures_md_connection_handler.h"
#include "common/ini_config.hpp"
#include "core/websocket/market_data/exchange_traits.h"
#include "core/websocket/market_data/onepass_binance_futures_md_decoder.hpp"
#include "core/websocket/market_data/json_binance_futures_md_decoder.hpp"
#include "schema/futures/response/api_response.h"
#include "schema/futures/response/book_ticker.h"
#include "schema/futures/response/depth_stream.h"
#include "schema/futures/response/exchange_info_response.h"
#include "schema/futures/response/session_response.h"
#include "schema/futures/response/snapshot.h"
#include "schema/futures/response/trade.h"

struct BinanceFuturesTraits {
  using ConnectionHandler = BinanceFuturesMdConnectionHandler;
  using SbeOps = std::monostate;
  using Formatter = BinanceFuturesFormatter;
  using Encoder = BinanceFuturesEncoder;
  using MdDomainConverter = BinanceFuturesMdMessageConverter;
  using DispatchRouter = BinanceDispatchRouter;
#ifdef USE_ONEPASS_DECODER
  using Decoder = core::OnepassBinanceFuturesMdDecoder;
#else
  using Decoder = core::JsonBinanceFuturesMdDecoder;
#endif

  using SbeDepthResponse = std::monostate;
  using SbeTradeEvent = std::monostate;
  using SbeDepthSnapshot = std::monostate;
  using SbeBestBidAsk = std::monostate;
  using ExchangeInfoResponse = schema::futures::ExchangeInfoHttpResponse;
  using ModifyOrderResponse = std::monostate;

  static constexpr bool uses_http_exchange_info() { return true; }
  static std::string get_exchange_info_url() {
    return INI_CONFIG.get("exchange",
        "exchange_info_url",
        "https://fapi.binance.com/fapi/v1/exchangeInfo");
  }

  using DepthResponse = schema::futures::DepthResponse;
  using TradeEvent = schema::futures::TradeEvent;
  using BookTickerEvent = schema::futures::BookTickerEvent;
  using ApiResponse = schema::futures::ApiResponse;
  using DepthSnapshot = schema::futures::DepthSnapshot;
  using SessionLogOnResponse = std::monostate;

  using WireMessage = std::variant<std::monostate, DepthResponse, TradeEvent,
      BookTickerEvent, DepthSnapshot, ApiResponse, ExchangeInfoResponse>;

  static constexpr std::string_view exchange_name() { return "Binance"; }
  static constexpr std::string_view market_type() { return "Futures"; }

  static std::string get_api_host() {
    return INI_CONFIG.get("exchange", "md_api_host", "ws-fapi.binance.com");
  }

  static std::string get_stream_host() {
    return INI_CONFIG.get("exchange", "md_stream_host", "fstream.binance.com");
  }

  static std::string get_api_endpoint_path() {
    return INI_CONFIG.get("exchange",
        "md_api_endpoint_path",
        "/ws-fapi/v1?returnRateLimits=false");
  }

  static std::string get_stream_endpoint_path() {
    return INI_CONFIG.get_with_symbol("exchange",
        "md_ws_path",
        "/stream?streams=btcusdt@depth/btcusdt@aggTrade");
  }

  static constexpr int kDefaultPort = 443;

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
  static constexpr bool supports_sbe() { return false; }
};

static_assert(ExchangeTraits<BinanceFuturesTraits>,
    "BinanceFuturesTraits must satisfy ExchangeTraits concept");

#endif  //BINANCE_FUTURES_TRAITS_H
