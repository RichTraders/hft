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
};

#endif  //BINANCE_SPOT_OE_DISPATCHER_H
