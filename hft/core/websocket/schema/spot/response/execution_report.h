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

#ifndef EXECUTION_REPORT_H
#define EXECUTION_REPORT_H
#include <glaze/glaze.hpp>

namespace schema {
struct ExecutionReportData {
  std::string event_type;   // "e"
  std::int64_t event_time;  // "E"

  std::string symbol;             // "s"
  std::uint64_t client_order_id;  // "c"
  std::string side;               // "S" (BUY / SELL)
  std::string order_type;         // "o" (LIMIT, MARKET, ...)
  std::string time_in_force;      // "f" (GTC, IOC, ...)

  double order_quantity{0.};    // "q"
  double order_price{0.};       // "p"
  double stop_price{0.};        // "P"
  double iceberg_quantity{0.};  // "F"

  std::int64_t order_list_id;            // "g"
  std::string original_client_order_id;  // "C"

  std::string execution_type;          // "x" (NEW, TRADE, CANCELED, ...)
  std::string order_status;            // "X" (NEW, FILLED, ...)
  std::string reject_reason = "NONE";  // "r"

  std::int64_t order_id;

  double last_executed_quantity{0.};      // "l"
  double cumulative_filled_quantity{0.};  // "z"
  double last_executed_price{0.};         // "L"

  double commission_amount{0.};                 // "n"
  std::optional<std::string> commission_asset;  // "N" (nullable)

  std::int64_t transaction_time;    // "T"
  std::int64_t trade_id;             // "t"
  std::int64_t prevented_match_id;  // "v"
  std::int64_t execution_id;        // "I"

  bool is_on_book;     // "w"
  bool is_maker_side;  // "m"
  bool ignore_flag;    // "M"

  std::int64_t order_creation_time;  // "O"

  double cumulative_quote_quantity{0.};  // "Z"
  double last_quote_quantity{0.};        // "Y"
  double quote_order_quantity{0.};       // "Q"

  std::uint64_t working_time;                       // "W"
  std::string self_trade_prevention_mode = "NONE";  // "V"

  // clang-format off
  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = ExecutionReportData;
    static constexpr auto value = glz::object(  // NOLINT(readability-identifier-naming)
      "e", &T::event_type,
      "E", &T::event_time,

      "s", &T::symbol,
      "c", glz::quoted_num<&T::client_order_id>,
      "S", &T::side,
      "o", &T::order_type,
      "f", &T::time_in_force,

      "q", glz::quoted_num<&T::order_quantity>,
      "p", glz::quoted_num<&T::order_price>,
      "P", glz::quoted_num<&T::stop_price>,
      "F", glz::quoted_num<&T::iceberg_quantity>,

      "g", &T::order_list_id,
      "C", &T::original_client_order_id,

      "x", &T::execution_type,
      "X", &T::order_status,
      "r", &T::reject_reason,

      "i", &T::order_id,

      "l", glz::quoted_num<&T::last_executed_quantity>,
      "z", glz::quoted_num<&T::cumulative_filled_quantity>,
      "L", glz::quoted_num<&T::last_executed_price>,

      "n", glz::quoted_num<&T::commission_amount>,
      "N", &T::commission_asset,

      "T", &T::transaction_time,
      "t", &T::trade_id,
      "v", &T::prevented_match_id,
      "I", &T::execution_id,

      "w", &T::is_on_book,
      "m", &T::is_maker_side,
      "M", &T::ignore_flag,
      "O", &T::order_creation_time,
      "Z", glz::quoted_num<&T::cumulative_quote_quantity>,
      "Y", glz::quoted_num<&T::last_quote_quantity>,
      "Q", glz::quoted_num<&T::quote_order_quantity>,
      "W", &T::working_time,
      "V", &T::self_trade_prevention_mode);
  };
  // clang-format on
};

struct ExecutionReportResponse {
  int subscription_id;
  ExecutionReportData event;

  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = ExecutionReportResponse;
    static constexpr auto value =  // NOLINT(readability-identifier-naming)
        glz::object("subscriptionId", &T::subscription_id, "event", &T::event);
  };
};
}  // namespace schema
#endif  //EXECUTION_REPORT_H
