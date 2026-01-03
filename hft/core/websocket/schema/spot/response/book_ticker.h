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
namespace schema {
struct BookTickerData {
  std::uint64_t update_id;
  std::string symbol;

  double best_bid_price;
  double best_bid_qty;
  double best_ask_price;
  double best_ask_qty;

  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = BookTickerData;
    static constexpr auto value = glz::object("u", &T::update_id, "s",  // NOLINT(readability-identifier-naming)
        &T::symbol, "b", glz::quoted_num<&T::best_bid_price>, "B",
        glz::quoted_num<&T::best_bid_qty>, "a",
        glz::quoted_num<&T::best_ask_price>, "A",
        glz::quoted_num<&T::best_ask_qty>);
  };
};

struct BookTicker {
  std::string stream;
  BookTickerData data;

  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = BookTicker;
    static constexpr auto value =  // NOLINT(readability-identifier-naming)
        glz::object("stream", &T::stream, "data", &T::data);
  };
};
}
}  // namespace schema
#endif  //BOOK_TICKER_H
