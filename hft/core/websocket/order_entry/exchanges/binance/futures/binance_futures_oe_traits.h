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

#ifndef BINANCE_FUTURES_OE_TRAITS_H
#define BINANCE_FUTURES_OE_TRAITS_H

#include "binance_futures_oe_connection_handler.h"
#include "binance_futures_oe_dispatcher.h"
#include "binance_futures_oe_encoder.hpp"
#include "binance_futures_oe_mapper.h"
#include "schema/futures/request/cancel_order.h"
#include "schema/futures/request/modify_order.h"
#include "schema/futures/request/new_order.h"
#include "schema/futures/response/account.h"
#include "schema/futures/response/api_response.h"
#include "schema/futures/response/balance_response.h"
#include "schema/futures/response/cancel_order_response.h"
#include "schema/futures/response/execution_report.h"
#include "schema/futures/response/modify_order_response.h"
#include "schema/futures/response/order.h"
#include "schema/futures/response/session_response.h"
#include "schema/futures/response/userdata_stream_response.h"

struct BinanceFuturesOeTraits {
  using ConnectionHandler = BinanceFuturesOeConnectionHandler;
  using DispatchRouter = BinanceFuturesOeDispatchRouter;
  using Encoder = core::BinanceFuturesOeEncoder;
  using Mapper = core::BinanceFuturesOeMapper;

  using PlaceOrderRequest = schema::futures::OrderPlaceRequest;
  using CancelOrderRequest = schema::futures::OrderCancelRequest;

  using ExecutionReportResponse = schema::futures::ExecutionReportResponse;
  using PlaceOrderResponse = schema::futures::PlaceOrderResponse;
  using CancelOrderResponse = schema::futures::CancelOrderResponse;
  using ApiResponse = schema::futures::ApiResponse;
  using CancelAndReorderResponse = std::monostate;
  using ModifyOrderResponse = schema::futures::ModifyOrderResponse;
  using CancelAllOrdersResponse = std::monostate;
  using SessionLogonResponse = schema::futures::SessionLogonResponse;
  using SessionUserSubscriptionResponse =
      schema::futures::UserDataStreamStartResponse;
  using SessionUserUnsubscriptionResponse =
      schema::futures::UserDataStreamStopResponse;
  using BalanceUpdateEnvelope = schema::futures::AccountBalanceResponse;
  using OutboundAccountPositionEnvelope =
      schema::futures::FuturesAccountInfoResponse;

  using WireMessage = std::variant<std::monostate, ExecutionReportResponse,
      SessionLogonResponse, CancelOrderResponse,
      SessionUserSubscriptionResponse, SessionUserUnsubscriptionResponse,
      ModifyOrderResponse, PlaceOrderResponse, BalanceUpdateEnvelope,
      OutboundAccountPositionEnvelope, ApiResponse>;

  static constexpr std::string_view exchange_name() { return "Binance"; }
  static constexpr std::string_view market_type() { return "Futures"; }

  static constexpr std::string_view get_api_host() {
    return "ws-fapi.binance.com";
  }

  static constexpr std::string_view get_api_endpoint_path() {
    return "/ws-fapi/v1?returnRateLimits=false";
  }

  static constexpr int kPort = 443;
  static constexpr int kKeepaliveMinutes = 60;
  static constexpr int kSecondsPerMinute = 60;
  static constexpr int kMsPerSecond = 1000;

  static constexpr int get_api_port() { return kPort; }

  static constexpr bool use_ssl() { return true; }

  static constexpr bool supports_position_side() { return true; }
  static constexpr bool supports_reduce_only() { return true; }
  static constexpr bool supports_cancel_and_reorder() { return false; }

  static constexpr bool requires_listen_key() { return true; }
  static constexpr bool requires_stream_transport() { return true; }
  static constexpr bool requires_signature_logon() { return true; }

  static constexpr int get_keepalive_interval_ms() {
    return kKeepaliveMinutes * kSecondsPerMinute * kMsPerSecond;
  }

  static constexpr std::string_view get_stream_host() {
    return "fstream.binance.com";
  }

  static constexpr std::string_view get_stream_endpoint_path() { return "/ws"; }

  static constexpr int get_stream_port() { return kPort; }
};

#endif  // BINANCE_FUTURES_OE_TRAITS_H
