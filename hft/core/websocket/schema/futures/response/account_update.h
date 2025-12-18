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

#ifndef FUTURES_ACCOUNT_UPDATE_H
#define FUTURES_ACCOUNT_UPDATE_H
#include <glaze/glaze.hpp>

namespace schema {
namespace futures {

struct AccountUpdateBalance {
  std::string asset;           // "a"
  double wallet_balance{};     // "wb"
  double cross_wallet{};       // "cw"
  double balance_change{};     // "bc"

  // clang-format off
  struct glaze {
    using T = AccountUpdateBalance;
    static constexpr auto value = glz::object(
      "a",  &T::asset,
      "wb", glz::quoted_num<&T::wallet_balance>,
      "cw", glz::quoted_num<&T::cross_wallet>,
      "bc", glz::quoted_num<&T::balance_change>
    );
  };
  // clang-format on
};

struct AccountUpdatePosition {
  std::string symbol;            // "s"
  double position_amount{};      // "pa"
  double entry_price{};          // "ep"
  double cumulative_realized{};  // "cr" (cumulative realized PnL)
  double unrealized_pnl{};       // "up"
  std::string margin_type;       // "mt" ("cross" or "isolated")
  double isolated_wallet{};      // "iw"
  std::string position_side;     // "ps" ("LONG", "SHORT", "BOTH")
  std::string margin_asset;      // "ma"
  double break_even_price{};     // "bep"

  // clang-format off
  struct glaze {
    using T = AccountUpdatePosition;
    static constexpr auto value = glz::object(
      "s",   &T::symbol,
      "pa",  glz::quoted_num<&T::position_amount>,
      "ep",  glz::quoted_num<&T::entry_price>,
      "cr",  glz::quoted_num<&T::cumulative_realized>,
      "up",  glz::quoted_num<&T::unrealized_pnl>,
      "mt",  &T::margin_type,
      "iw",  glz::quoted_num<&T::isolated_wallet>,
      "ps",  &T::position_side,
      "ma",  &T::margin_asset,
      "bep", glz::quoted_num<&T::break_even_price>
    );
  };
  // clang-format on
};

struct AccountUpdateData {
  std::vector<AccountUpdateBalance> balances;   // "B"
  std::vector<AccountUpdatePosition> positions; // "P"
  std::string reason;                           // "m" (event reason)

  // clang-format off
  struct glaze {
    using T = AccountUpdateData;
    static constexpr auto value = glz::object(
      "B", &T::balances,
      "P", &T::positions,
      "m", &T::reason
    );
  };
  // clang-format on
};

struct AccountUpdateResponse {
  std::string event_type;           // "e" = "ACCOUNT_UPDATE"
  std::int64_t transaction_time{};  // "T"
  std::int64_t event_time{};        // "E"
  AccountUpdateData data;           // "a"

  // clang-format off
  struct glaze {
    using T = AccountUpdateResponse;
    static constexpr auto value = glz::object(
      "e", &T::event_type,
      "T", &T::transaction_time,
      "E", &T::event_time,
      "a", &T::data
    );
  };
  // clang-format on
};

}  // namespace futures
}  // namespace schema
#endif  // FUTURES_ACCOUNT_UPDATE_H
