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
 
 #ifndef TRADE_H
#define TRADE_H

#include <glaze/glaze.hpp>
#include "schema/price_qty_array.h"

namespace schema::futures {

using ScaledPrice = ::ScaledInt64<::common::FixedPointConfig::kPriceScale>;
using ScaledQty = ::ScaledInt64<::common::FixedPointConfig::kQtyScale>;

struct AggregateTradeEvent {
  std::string event_type;           // "e"
  std::int64_t event_time;          // "E"
  std::string symbol;               // "s"
  std::int64_t aggregate_trade_id;  // "a"
  ScaledPrice price;                // "p"
  ScaledQty quantity;               // "q"
  std::int64_t first_trade_id;      // "f"
  std::int64_t last_trade_id;       // "l"
  std::int64_t trade_time;          // "T"
  bool is_buyer_market_maker;       // "m"

  // clang-format off
  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = AggregateTradeEvent;

    static constexpr auto value = glz::object(  // NOLINT(readability-identifier-naming)  // NOLINT(readability-identifier-naming)
        "e", &T::event_type,
        "E", &T::event_time,
        "s", &T::symbol,
        "a", &T::aggregate_trade_id,
        "p", &T::price,
        "q", &T::quantity,
        "f", &T::first_trade_id,
        "l", &T::last_trade_id,
        "T", &T::trade_time,
        "m", &T::is_buyer_market_maker);
    // clang-format on
  };
};
struct TradeEvent {
  std::string stream;
  AggregateTradeEvent data;

  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = TradeEvent;
    // NOLINTNEXTLINE(readability-identifier-naming)
    static constexpr auto value =  // NOLINT(readability-identifier-naming)
        glz::object("stream", &T::stream, "data", &T::data);
  };
};
}  // namespace schema::futures
#endif //TRADE_H
