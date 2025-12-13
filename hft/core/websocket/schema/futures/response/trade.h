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
namespace schema {
namespace futures {
struct AggregateTradeEvent {
  std::string event_type;           // "e"
  std::int64_t event_time;          // "E"
  std::string symbol;               // "s"
  std::int64_t aggregate_trade_id;  // "a"
  double price;                     // "p"
  double quantity;                  // "q"
  std::int64_t first_trade_id;      // "f"
  std::int64_t last_trade_id;       // "l"
  std::int64_t trade_time;          // "T"
  bool is_buyer_market_maker;       // "m"

  // clang-format off
  struct glaze {
    using T = AggregateTradeEvent;

    static constexpr auto value = glz::object(
        "e", &T::event_type,
        "E", &T::event_time,
        "s", &T::symbol,
        "a", &T::aggregate_trade_id,
        "p", glz::quoted_num<&T::price>,
        "q", glz::quoted_num<&T::quantity>,
        "f", &T::first_trade_id,
        "l", &T::last_trade_id,
        "T", &T::trade_time,
        "m", &T::is_buyer_market_maker);
    // clang-format on
  };
};
}
}
#endif //TRADE_H
