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

namespace schema::futures {

struct BookTickerData {
  std::string event_type;      // "e" - event type (bookTicker)
  std::uint64_t update_id;     // "u" - order book updateId
  std::int64_t event_time;     // "E" - event time
  std::int64_t transaction_time;  // "T" - transaction time
  std::string symbol;          // "s" - symbol
  int64_t best_bid_price;      // "b" - best bid price
  int64_t best_bid_qty;        // "B" - best bid qty
  int64_t best_ask_price;      // "a" - best ask price
  int64_t best_ask_qty;        // "A" - best ask qty

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
        "b", glz::quoted_num<&T::best_bid_price>,
        "B", glz::quoted_num<&T::best_bid_qty>,
        "a", glz::quoted_num<&T::best_ask_price>,
        "A", glz::quoted_num<&T::best_ask_qty>);
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
