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

#include "binance_spot_oe_dispatcher.h"
#include "binance_spot_oe_encoder.h"
#include "binance_spot_oe_mapper.h"
#include "diabled_listen_key_manager.h"
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

  static constexpr std::string_view get_api_host() {
    return "ws-api.binance.com";
  }

  static constexpr std::string_view get_api_endpoint_path() {
    return "/ws-api/v3?returnRateLimits=false";
  }

  static constexpr int kPort = 9443;

  static constexpr int get_api_port() { return kPort; }

  static constexpr bool use_ssl() { return true; }

  static constexpr bool supports_position_side() { return false; }
  static constexpr bool supports_reduce_only() { return false; }

  using ListenKeyManager = DisabledListenKeyManager;
  static constexpr bool requires_listen_key() { return false; }
  static constexpr bool requires_signature_logon() { return true; }
};

#endif  // BINANCE_SPOT_OE_TRAITS_H
