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

#ifndef FUTURES_EXECUTION_REPORT_H
#define FUTURES_EXECUTION_REPORT_H
#include <glaze/glaze.hpp>

namespace schema::futures {
struct OrderUpdateInfo {
  std::string symbol;         // "s"
  uint64_t client_order_id;   // "c"
  std::string side;           // "S"
  std::string order_type;     // "o"
  std::string time_in_force;  // "f"

  double order_quantity{};  // "q"
  double order_price{};     // "p"
  double average_price{};   // "ap"
  double stop_price{};      // "sp"

  std::string execution_type;  // "x"
  std::string order_status;    // "X"

  std::uint64_t order_id{};  // "i"

  double last_executed_quantity{};      // "l"
  double cumulative_filled_quantity{};  // "z"
  double last_filled_price{};           // "L"

  std::string commission_asset;  // "N"
  double commission{};           // "n"

  std::int64_t trade_time{};  // "T"
  std::int64_t trade_id{};    // "t"

  double bids_notional{};  // "b"
  double ask_notional{};   // "a"

  bool is_maker{};        // "m"
  bool is_reduce_only{};  // "R"

  std::string working_type;         // "wt"
  std::string original_order_type;  // "ot"
  std::string position_side;        // "ps"

  bool is_close_all{};  // "cp"

  double activation_price{};  // "AP"
  double callback_rate{};     // "cr"

  bool price_protection{};  // "pP"

  std::uint64_t ignore_si{};  // "si"
  std::uint64_t ignore_ss{};  // "ss"

  double realized_profit{};  // "rp"

  std::string stp_mode;          // "V"
  std::string price_match_mode;  // "pm"

  std::int64_t gtd_auto_cancel_time{};  // "gtd"
  std::string reject_reason;            // "er"

  // clang-format off
  // NOLINTNEXTLINE(readability-identifier-naming)
    struct glaze {
        using T = OrderUpdateInfo;
        static constexpr auto value = glz::object(  // NOLINT(readability-identifier-naming)
            "s",  &T::symbol,
            "c",  glz::quoted_num<&T::client_order_id>,
            "S",  &T::side,
            "o",  &T::order_type,
            "f",  &T::time_in_force,

            "q",  glz::quoted_num<&T::order_quantity>,
            "p",  glz::quoted_num<&T::order_price>,
            "ap", glz::quoted_num<&T::average_price>,
            "sp", glz::quoted_num<&T::stop_price>,

            "x",  &T::execution_type,
            "X",  &T::order_status,
            "i",  &T::order_id,

            "l",  glz::quoted_num<&T::last_executed_quantity>,
            "z",  glz::quoted_num<&T::cumulative_filled_quantity>,
            "L",  glz::quoted_num<&T::last_filled_price>,

            "N",  &T::commission_asset,
            "n",  glz::quoted_num<&T::commission>,

            "T",  &T::trade_time,
            "t",  &T::trade_id,

            "b",  glz::quoted_num<&T::bids_notional>,
            "a",  glz::quoted_num<&T::ask_notional>,

            "m",  &T::is_maker,
            "R",  &T::is_reduce_only,
            "wt", &T::working_type,
            "ot", &T::original_order_type,
            "ps", &T::position_side,
            "cp", &T::is_close_all,

            "AP", glz::quoted_num<&T::activation_price>,
            "cr", glz::quoted_num<&T::callback_rate>,

            "pP", &T::price_protection,
            "si", &T::ignore_si,
            "ss", &T::ignore_ss,

            "rp", glz::quoted_num<&T::realized_profit>,

            "V",  &T::stp_mode,
            "pm", &T::price_match_mode,
            "gtd",&T::gtd_auto_cancel_time,
            "er", &T::reject_reason
        );
    };
  // clang-format on
};

struct ExecutionReportResponse {
  std::string event_type;           // "e"
  std::int64_t event_time{};        // "E"
  std::int64_t transaction_time{};  // "T"

  OrderUpdateInfo event;  // "o"

  // clang-format off
  // NOLINTNEXTLINE(readability-identifier-naming)
    struct glaze {
        using T = ExecutionReportResponse;
        static constexpr auto value = glz::object(  // NOLINT(readability-identifier-naming)
            "e", &T::event_type,
            "E", &T::event_time,
            "T", &T::transaction_time,
            "o", &T::event
        );
    };
  // clang-format on
};
}  // namespace schema::futures
#endif  //FUTURES_EXECUTION_REPORT_H