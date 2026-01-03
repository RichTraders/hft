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

#ifndef ACCOUNT_RESPONSE_H
#define ACCOUNT_RESPONSE_H
#include <glaze/glaze.hpp>
#include "api_response.h"
namespace schema::futures {
struct AccountBalanceResponse {
  std::string id;
  int status{0};

  struct AssetBalance {
    std::string accountAlias;
    std::string asset;
    std::string balance;
    std::string crossWalletBalance;
    std::string crossUnPnl;
    std::string availableBalance;
    std::string maxWithdrawAmount;
    bool marginAvailable;
    std::uint64_t updateTime;

  // NOLINTNEXTLINE(readability-identifier-naming)
    struct glaze {
      using T = AssetBalance;
      // clang-format off
      static constexpr auto value = glz::object(  // NOLINT(readability-identifier-naming)
        "accountAlias", &T::accountAlias,
        "asset", &T::asset,
        "balance", &T::balance,
        "crossWalletBalance", &T::crossWalletBalance,
        "crossUnPnl", &T::crossUnPnl,
        "availableBalance", &T::availableBalance,
        "maxWithdrawAmount", &T::maxWithdrawAmount,
        "marginAvailable", &T::marginAvailable,
        "updateTime", &T::updateTime
      );
      // clang-format on
    };
  };
  std::vector<AssetBalance> event;
  std::optional<std::vector<RateLimit>> rate_limits;

  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = AccountBalanceResponse;
    // clang-format off
    static constexpr auto value = glz::object(  // NOLINT(readability-identifier-naming)
      "id", &T::id,
      "status", &T::status,
      "result", &T::event,
      "rateLimits", &T::rate_limits
    );
    // clang-format on
  };
};

inline std::ostream& operator<<(std::ostream& stream, const AccountBalanceResponse::AssetBalance& asset) {
  stream << "{accountAlias:" << asset.accountAlias
     << ", asset:" << asset.asset
     << ", balance:" << asset.balance
     << ", available:" << asset.availableBalance << "}";
  return stream;
}

inline std::ostream& operator<<(std::ostream& stream, const AccountBalanceResponse& response) {
  stream << "id:" << response.id << ", status:" << response.status << ", result:[";
  for (size_t i = 0; i < response.event.size(); ++i) {
    stream << response.event[i] << (i < response.event.size() - 1 ? ", " : "");
  }
  stream << "]";
  return stream;
}
}  // namespace schema::futures
#endif  // ACCOUNT_RESPONSE_H
