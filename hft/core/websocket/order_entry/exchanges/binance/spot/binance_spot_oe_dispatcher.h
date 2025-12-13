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

#ifndef BINANCE_SPOT_OE_DISPATCHER_H
#define BINANCE_SPOT_OE_DISPATCHER_H

#include "schema/spot/response/account_position.h"
#include "schema/spot/response/api_response.h"
#include "schema/spot/response/execution_report.h"
#include "schema/spot/response/order.h"
#include "schema/spot/response/session_response.h"
#include "websocket/order_entry/ws_oe_dispatcher_context.h"

struct BinanceSpotOeDispatchRouter {
  static constexpr int kHttpOK = 200;

  template <typename T>
  static constexpr std::optional<std::string_view> get_dispatch_type(
      const T& msg) {
    using MsgType = std::decay_t<T>;

    // Execution Report → "8"
    if constexpr (std::is_same_v<MsgType, schema::ExecutionReportResponse>) {
      // Check for cancel reject
      if (msg.event.execution_type == "CANCELED" &&
          msg.event.reject_reason != "NONE") {
        return "9";  // Cancel reject
      }
      return "8";  // Regular execution report
    } else if constexpr (std::is_same_v<MsgType,
                             schema::SessionLogonResponse>) {
      return "A";
    } else if constexpr (std::is_same_v<MsgType, schema::ApiResponse>) {
      if (msg.status != kHttpOK) {
        return "8";
      }
      return std::nullopt;
    }

    if constexpr (std::is_same_v<MsgType, schema::CancelAndReorderResponse> ||
                  std::is_same_v<MsgType, schema::PlaceOrderResponse> ||
                  std::is_same_v<MsgType, schema::CancelAllOrdersResponse> ||
                  std::is_same_v<MsgType, schema::BalanceUpdateEnvelope> ||
                  std::is_same_v<MsgType,
                      schema::OutboundAccountPositionEnvelope>) {
      return std::nullopt;
    }
    // Unknown → no dispatch
    return std::nullopt;
  }

  // NEW: Main entry point for message processing
  template <typename ExchangeTraits>
  static void process_message(
      const typename ExchangeTraits::WireMessage& message,
      const core::WsOeDispatchContext<ExchangeTraits>& context);

  template <typename WireMessage>
  static void process_api_message(const WireMessage& message);

 private:
  // Handler methods (all static template methods)
  template <typename ExchangeTraits>
  static void handle_execution_report(
      const typename ExchangeTraits::ExecutionReportResponse& report,
      const core::WsOeDispatchContext<ExchangeTraits>& context,
      const typename ExchangeTraits::WireMessage& message);

  template <typename ExchangeTraits>
  static void handle_session_logon(
      const typename ExchangeTraits::SessionLogonResponse& response,
      const core::WsOeDispatchContext<ExchangeTraits>& context,
      const typename ExchangeTraits::WireMessage& message);

  template <typename ExchangeTraits>
  static void handle_user_subscription(
      const typename ExchangeTraits::SessionUserSubscriptionResponse& response,
      const core::WsOeDispatchContext<ExchangeTraits>& context,
      const typename ExchangeTraits::WireMessage& message);

  template <typename ExchangeTraits>
  static void handle_api_response(
      const typename ExchangeTraits::ApiResponse& response,
      const core::WsOeDispatchContext<ExchangeTraits>& context,
      const typename ExchangeTraits::WireMessage& message);

  template <typename ExchangeTraits>
  static void handle_cancel_and_reorder_response(
      const typename ExchangeTraits::CancelAndReorderResponse& response,
      const core::WsOeDispatchContext<ExchangeTraits>& context,
      const typename ExchangeTraits::WireMessage& message);

  template <typename ExchangeTraits>
  static void handle_modify_order_response(
      const typename ExchangeTraits::ModifyOrderResponse& response,
      const core::WsOeDispatchContext<ExchangeTraits>& context,
      const typename ExchangeTraits::WireMessage& message);

  template <typename ExchangeTraits>
  static void handle_cancel_all_response(
      const typename ExchangeTraits::CancelAllOrdersResponse& response,
      const core::WsOeDispatchContext<ExchangeTraits>& context,
      const typename ExchangeTraits::WireMessage& message);

  template <typename ExchangeTraits>
  static void handle_place_order_response(
      const typename ExchangeTraits::PlaceOrderResponse& response,
      const core::WsOeDispatchContext<ExchangeTraits>& context,
      const typename ExchangeTraits::WireMessage& message);

  template <typename ExchangeTraits>
  static void handle_balance_update(
      const typename ExchangeTraits::BalanceUpdateEnvelope& envelope,
      const core::WsOeDispatchContext<ExchangeTraits>& context);

  template <typename ExchangeTraits>
  static void handle_account_updated(
      const typename ExchangeTraits::OutboundAccountPositionEnvelope& envelope,
      const core::WsOeDispatchContext<ExchangeTraits>& context);
};

#include "binance_spot_oe_dispatcher.tpp"

#endif  //BINANCE_SPOT_OE_DISPATCHER_H
