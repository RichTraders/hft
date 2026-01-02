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

#ifndef FUTURES_ACCOUNT_H
#define FUTURES_ACCOUNT_H

#include "api_response.h"
#include "glaze/glaze.hpp"
namespace schema::futures {
struct FuturesAccountInfoResponse {
  std::string id;
  int status{0};

  struct AccountResult {
    std::string totalInitialMargin;
    std::string totalMaintMargin;
    std::string totalWalletBalance;
    std::string totalUnrealizedProfit;
    std::string totalMarginBalance;
    std::string totalPositionInitialMargin;
    std::string totalOpenOrderInitialMargin;
    std::string totalCrossWalletBalance;
    std::string totalCrossUnPnl;
    std::string availableBalance;
    std::string maxWithdrawAmount;

    struct Asset {
      std::string asset;
      std::string walletBalance;
      std::string unrealizedProfit;
      std::string marginBalance;
      std::string maintMargin;
      std::string initialMargin;
      std::string positionInitialMargin;
      std::string openOrderInitialMargin;
      std::string crossWalletBalance;
      std::string crossUnPnl;
      std::string availableBalance;
      std::string maxWithdrawAmount;
      std::int64_t updateTime;

  // NOLINTNEXTLINE(readability-identifier-naming)
      struct glaze {
        using T = Asset;
        // clang-format off
        static constexpr auto value = glz::object(  // NOLINT(readability-identifier-naming)
          "asset", &T::asset,
          "walletBalance", &T::walletBalance,
          "unrealizedProfit", &T::unrealizedProfit,
          "marginBalance", &T::marginBalance,
          "maintMargin", &T::maintMargin,
          "initialMargin", &T::initialMargin,
          "positionInitialMargin", &T::positionInitialMargin,
          "openOrderInitialMargin", &T::openOrderInitialMargin,
          "crossWalletBalance", &T::crossWalletBalance,
          "crossUnPnl", &T::crossUnPnl,
          "availableBalance", &T::availableBalance,
          "maxWithdrawAmount", &T::maxWithdrawAmount,
          "updateTime", &T::updateTime
        );
        // clang-format on
      };
    };

    struct Position {
      std::string symbol;
      std::string positionSide;
      std::string positionAmt;
      std::string unrealizedProfit;
      std::string isolatedMargin;
      std::string notional;
      std::string isolatedWallet;
      std::string initialMargin;
      std::string maintMargin;
      std::int64_t updateTime;

  // NOLINTNEXTLINE(readability-identifier-naming)
      struct glaze {
        using T = Position;
        // clang-format off
        static constexpr auto value = glz::object(  // NOLINT(readability-identifier-naming)
          "symbol", &T::symbol,
          "positionSide", &T::positionSide,
          "positionAmt", &T::positionAmt,
          "unrealizedProfit", &T::unrealizedProfit,
          "isolatedMargin", &T::isolatedMargin,
          "notional", &T::notional,
          "isolatedWallet", &T::isolatedWallet,
          "initialMargin", &T::initialMargin,
          "maintMargin", &T::maintMargin,
          "updateTime", &T::updateTime
        );
        // clang-format on
      };
    };

    std::vector<Asset> assets;
    std::vector<Position> positions;

  // NOLINTNEXTLINE(readability-identifier-naming)
    struct glaze {
      using T = AccountResult;
      // clang-format off
      static constexpr auto value = glz::object(  // NOLINT(readability-identifier-naming)
        "totalInitialMargin", &T::totalInitialMargin,
        "totalMaintMargin", &T::totalMaintMargin,
        "totalWalletBalance", &T::totalWalletBalance,
        "totalUnrealizedProfit", &T::totalUnrealizedProfit,
        "totalMarginBalance", &T::totalMarginBalance,
        "totalPositionInitialMargin", &T::totalPositionInitialMargin,
        "totalOpenOrderInitialMargin", &T::totalOpenOrderInitialMargin,
        "totalCrossWalletBalance", &T::totalCrossWalletBalance,
        "totalCrossUnPnl", &T::totalCrossUnPnl,
        "availableBalance", &T::availableBalance,
        "maxWithdrawAmount", &T::maxWithdrawAmount,
        "assets", &T::assets,
        "positions", &T::positions
      );
      // clang-format on
    };
  };

  AccountResult event;
  std::optional<std::vector<RateLimit>> rate_limits;

  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = FuturesAccountInfoResponse;
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

inline std::ostream& operator<<(std::ostream& stream,
    const FuturesAccountInfoResponse::AccountResult::Asset& asset) {
  stream << "{asset:" << asset.asset
         << ", walletBalance:" << asset.walletBalance
         << ", unrealizedProfit:" << asset.unrealizedProfit << "}";
  return stream;
}

inline std::ostream& operator<<(std::ostream& stream,
    const FuturesAccountInfoResponse::AccountResult::Position& position) {
  stream << "{symbol:" << position.symbol << ", side:" << position.positionSide
         << ", amt:" << position.positionAmt
         << ", pnl:" << position.unrealizedProfit << "}";
  return stream;
}

inline std::ostream& operator<<(std::ostream& stream,
    const FuturesAccountInfoResponse::AccountResult& account) {
  stream << "totalWalletBalance:" << account.totalWalletBalance
         << ", totalUnPnl:" << account.totalUnrealizedProfit
         << ", availableBalance:" << account.availableBalance << ", assets:[";
  for (size_t i = 0; i < account.assets.size(); ++i) {
    stream << account.assets[i] << (i < account.assets.size() - 1 ? ", " : "");
  }
  stream << "], positions:[";
  for (size_t i = 0; i < account.positions.size(); ++i) {
    stream << account.positions[i]
           << (i < account.positions.size() - 1 ? ", " : "");
  }
  stream << "]";
  return stream;
}

inline std::ostream& operator<<(std::ostream& stream,
    const FuturesAccountInfoResponse& response) {
  stream << "id:" << response.id << ", status:" << response.status
         << ", result:{" << response.event << "}";
  return stream;
}
}  // namespace schema::futures
#endif  //FUTURES_ACCOUNT_H
