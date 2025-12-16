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

#ifndef BINANCE_SPOT_OE_TRAITS_H
#define BINANCE_SPOT_OE_TRAITS_H

#include "binance_spot_oe_connection_handler.h"
#include "binance_spot_oe_dispatcher.h"
#include "binance_spot_oe_encoder.h"
#include "binance_spot_oe_mapper.h"
#include "common/ini_config.hpp"
#include "core/websocket/order_entry/oe_exchange_traits.h"
#include "schema/spot/request/cancel_all_orders.h"
#include "schema/spot/request/cancel_and_reorder.h"
#include "schema/spot/request/order_cancel.h"
#include "schema/spot/request/order_request.h"
#include "schema/spot/response/account_position.h"
#include "schema/spot/response/api_response.h"
#include "schema/spot/response/execution_report.h"
#include "schema/spot/response/order.h"
#include "schema/spot/response/session_response.h"

struct BinanceSpotOeTraits {
  using ConnectionHandler = BinanceSpotOeConnectionHandler;
  using DispatchRouter = BinanceSpotOeDispatchRouter;
  using Encoder = core::BinanceSpotOeEncoder;
  using Mapper = core::BinanceSpotOeMapper;

  using PlaceOrderRequest = schema::OrderPlaceRequest;
  using CancelOrderRequest = schema::OrderCancelRequest;

  using ExecutionReportResponse = schema::ExecutionReportResponse;
  using PlaceOrderResponse = schema::PlaceOrderResponse;
  using CancelOrderResponse = schema::CancelOrderResponse;
  using ApiResponse = schema::ApiResponse;
  using CancelAndReorderResponse = schema::CancelAndReorderResponse;
  using ModifyOrderResponse = std::monostate;
  using CancelAllOrdersResponse = schema::CancelAllOrdersResponse;
  using SessionLogonResponse = schema::SessionLogonResponse;
  using SessionUserSubscriptionResponse =
      schema::SessionUserSubscriptionResponse;
  using SessionUserUnsubscriptionResponse =
      schema::SessionUserUnsubscriptionResponse;
  using BalanceUpdateEnvelope = schema::BalanceUpdateEnvelope;
  using OutboundAccountPositionEnvelope =
      schema::OutboundAccountPositionEnvelope;

  using WireMessage = std::variant<std::monostate, ExecutionReportResponse,
      SessionLogonResponse, CancelOrderResponse, CancelAllOrdersResponse,
      SessionUserSubscriptionResponse, SessionUserUnsubscriptionResponse,
      CancelAndReorderResponse, PlaceOrderResponse, BalanceUpdateEnvelope,
      OutboundAccountPositionEnvelope, ApiResponse>;

  static constexpr std::string_view exchange_name() { return "Binance"; }
  static constexpr std::string_view market_type() { return "Spot"; }

  static std::string get_api_host() {
    return INI_CONFIG.get("exchange", "oe_api_host", "ws-api.binance.com");
  }

  static std::string get_api_endpoint_path() {
    return INI_CONFIG.get("exchange",
        "oe_api_endpoint_path",
        "/ws-api/v3?returnRateLimits=false");
  }

  static constexpr int kDefaultPort = 9443;

  static int get_api_port() {
    return INI_CONFIG.get_int("exchange", "oe_port", kDefaultPort);
  }

  static bool use_ssl() {
    return INI_CONFIG.get_int("exchange", "oe_use_ssl", 1) != 0;
  }

  static constexpr bool supports_position_side() { return false; }
  static constexpr bool supports_reduce_only() { return false; }
  static constexpr bool supports_cancel_and_reorder() { return true; }

  static constexpr bool requires_listen_key() { return false; }
  static constexpr bool requires_stream_transport() { return false; }
  static constexpr bool requires_signature_logon() { return true; }
  static int get_keepalive_interval_ms() { return 0; }

  static std::string get_stream_host() { return ""; }
  static std::string get_stream_endpoint_path() { return ""; }
  static int get_stream_port() { return 0; }
};

static_assert(OeExchangeTraits<BinanceSpotOeTraits>,
    "BinanceSpotOeTraits must satisfy OeExchangeTraits concept");

#endif  // BINANCE_SPOT_OE_TRAITS_H
