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

#ifndef RISKMANAGER_H
#define RISKMANAGER_H
#include "position_keeper.h"
#include "types.h"

namespace trading {

enum class RiskCheckResult : uint8_t {
  kInvalid = 0,
  kOrderTooLarge = 1,
  kPositionTooLarge = 2,
  kLossTooLarge = 3,
  kAllowed = 4
};

inline auto riskCheckResultToString(RiskCheckResult result) {
  switch (result) {
    case RiskCheckResult::kInvalid:
      return "INVALID";
    case RiskCheckResult::kOrderTooLarge:
      return "ORDER_TOO_LARGE";
    case RiskCheckResult::kPositionTooLarge:
      return "POSITION_TOO_LARGE";
    case RiskCheckResult::kLossTooLarge:
      return "LOSS_TOO_LARGE";
    case RiskCheckResult::kAllowed:
      return "ALLOWED";
  }

  return "";
}

struct RiskInfo {
  const PositionInfo* position_info_ = nullptr;

  common::RiskCfg risk_cfg_;

  [[nodiscard]] auto checkPreTradeRisk(const common::Side side,
                                       const common::Qty qty) const noexcept {
    if (UNLIKELY(qty.value > risk_cfg_.max_order_size_.value))
      return RiskCheckResult::kOrderTooLarge;
    if (UNLIKELY(std::abs(position_info_->position_ +
                          sideToValue(side) * static_cast<int32_t>(qty.value)) >
                 static_cast<int32_t>(risk_cfg_.max_position_.value)))
      return RiskCheckResult::kPositionTooLarge;
    if (UNLIKELY(position_info_->total_pnl_ < risk_cfg_.max_loss_))
      return RiskCheckResult::kLossTooLarge;

    return RiskCheckResult::kAllowed;
  }

  [[nodiscard]] auto toString() const {
    std::ostringstream stream;
    stream << "RiskInfo" << "[" << "pos:" << position_info_->toString() << " "
           << risk_cfg_.toString() << "]";

    return stream.str();
  }
};

using TickerRiskInfoHashMap = std::unordered_map<std::string, RiskInfo>;

class RiskManager {
 public:
  RiskManager(common::Logger* logger, const PositionKeeper* position_keeper,
              const common::TradeEngineCfgHashMap& ticker_cfg);

  [[nodiscard]] auto checkPreTradeRisk(const common::TickerId& ticker_id,
                                       const common::Side side,
                                       const common::Qty qty) const noexcept {
    return ticker_risk_.at(ticker_id).checkPreTradeRisk(side, qty);
  }

  RiskManager() = delete;

  RiskManager(const RiskManager&) = delete;

  RiskManager(const RiskManager&&) = delete;

  RiskManager& operator=(const RiskManager&) = delete;

  RiskManager& operator=(const RiskManager&&) = delete;

 private:
  common::Logger* logger_;
  TickerRiskInfoHashMap ticker_risk_;
};
}  // namespace trading

#endif  //RISKMANAGER_H