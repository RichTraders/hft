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

#ifndef BINANCE_SPOT_DISPATCHER_H
#define BINANCE_SPOT_DISPATCHER_H
#include "schema/spot/response/depth_stream.h"
#include "schema/spot/response/exchange_info_response.h"
#include "schema/spot/response/snapshot.h"
#include "schema/spot/response/trade.h"
#include "schema/spot/sbe/best_bid_ask_sbe.h"
#include "schema/spot/sbe/depth_stream_sbe.h"
#include "schema/spot/sbe/snapshot_sbe.h"
#include "schema/spot/sbe/trade_sbe.h"

struct BinanceDispatchRouter {
  template <typename T>
  static constexpr std::optional<std::string_view> get_dispatch_type(
      const T& /*msg*/) {
    using MsgType = std::decay_t<T>;

    // Snapshot messages → "W"
    if constexpr (std::is_same_v<MsgType, schema::sbe::SbeDepthSnapshot> ||
                  std::is_same_v<MsgType, schema::DepthSnapshot>) {
      return "W";
    }
    // Market data updates → "X"
    else if constexpr (std::is_same_v<MsgType, schema::sbe::SbeDepthResponse> ||
                       std::is_same_v<MsgType, schema::DepthResponse> ||
                       std::is_same_v<MsgType, schema::sbe::SbeTradeEvent> ||
                       std::is_same_v<MsgType, schema::TradeEvent> ||
                       std::is_same_v<MsgType, schema::sbe::SbeBestBidAsk>) {
      return "X";
    }
    // Exchange info → "y"
    else if constexpr (std::is_same_v<MsgType, schema::ExchangeInfoResponse>) {
      return "y";
    } else {
      return std::nullopt;
    }
  }

  template <typename ExchangeTraits, typename DispatchFunc>
  static void process_message(
      const typename ExchangeTraits::WireMessage& message,
      DispatchFunc&& dispatch_fn) {
    std::visit(
        [&dispatch_fn](const auto& arg) {
          if (const auto type = get_dispatch_type(arg)) {
            dispatch_fn(*type);
          }
        },
        message);
  }
};
#endif  //BINANCE_SPOT_DISPATCHER_H
