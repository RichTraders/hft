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

#ifndef BOOK_TICKER_H
#define BOOK_TICKER_H

#include <glaze/glaze.hpp>
#include "schema/price_qty_array.h"

namespace schema::futures {

using ScaledPrice = ScaledInt64<common::FixedPointConfig::kPriceScale>;
using ScaledQty = ScaledInt64<common::FixedPointConfig::kQtyScale>;

struct BookTickerData {
  std::string event_type;        // "e" - event type (bookTicker)
  std::uint64_t update_id;       // "u" - order book updateId
  std::int64_t event_time;       // "E" - event time
  std::int64_t transaction_time; // "T" - transaction time
  std::string symbol;            // "s" - symbol
  ScaledPrice best_bid_price;    // "b" - best bid price
  ScaledQty best_bid_qty;        // "B" - best bid qty
  ScaledPrice best_ask_price;    // "a" - best ask price
  ScaledQty best_ask_qty;        // "A" - best ask qty

  // clang-format off
  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = BookTickerData;
    static constexpr auto value = glz::object(  // NOLINT(readability-identifier-naming)  // NOLINT(readability-identifier-naming)
        "e", &T::event_type,
        "u", &T::update_id,
        "E", &T::event_time,
        "T", &T::transaction_time,
        "s", &T::symbol,
        "b", &T::best_bid_price,
        "B", &T::best_bid_qty,
        "a", &T::best_ask_price,
        "A", &T::best_ask_qty);
  };
  // clang-format on
};

struct BookTickerEvent {
  std::string stream;
  BookTickerData data;

  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = BookTickerEvent;
    // NOLINTNEXTLINE(readability-identifier-naming)
    static constexpr auto value =  // NOLINT(readability-identifier-naming)
        glz::object("stream", &T::stream, "data", &T::data);
  };
};

}  // namespace schema::futures

#endif  // BOOK_TICKER_H
