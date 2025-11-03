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
  kPositionTooSmall = 3,
  kLossTooLarge = 4,
  kAllowed = 5
};

inline auto riskCheckResultToString(RiskCheckResult result) {
  switch (result) {
    case RiskCheckResult::kInvalid:
      return "INVALID";
    case RiskCheckResult::kOrderTooLarge:
      return "ORDER_TOO_LARGE";
    case RiskCheckResult::kPositionTooLarge:
      return "POSITION_TOO_LARGE";
    case RiskCheckResult::kPositionTooSmall:
      return "POSITION_TOO_Small";
    case RiskCheckResult::kLossTooLarge:
      return "LOSS_TOO_LARGE";
    case RiskCheckResult::kAllowed:
      return "ALLOWED";
  }

  return "";
}

struct RiskInfo {
  PositionInfo* position_info_ = nullptr;

  common::RiskCfg risk_cfg_;

  [[nodiscard]] RiskCheckResult checkPreTradeRisk(
      common::Side side, common::Qty qty, common::Qty reserved_position,
      common::Logger::Producer& logger) noexcept;

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
  RiskManager(common::Logger* logger, PositionKeeper* position_keeper,
              const common::TradeEngineCfgHashMap& ticker_cfg);

  ~RiskManager();
  [[nodiscard]] auto checkPreTradeRisk(
      const common::TickerId& ticker_id, const common::Side side,
      const common::Qty qty, const common::Qty reserved_qty) noexcept {
    return ticker_risk_.at(ticker_id).checkPreTradeRisk(side, qty, reserved_qty,
                                                        logger_);
  }

  RiskManager() = delete;

  RiskManager(const RiskManager&) = delete;

  RiskManager(const RiskManager&&) = delete;

  RiskManager& operator=(const RiskManager&) = delete;

  RiskManager& operator=(const RiskManager&&) = delete;

 private:
  common::Logger::Producer logger_;
  TickerRiskInfoHashMap ticker_risk_;
};
}  // namespace trading

#endif  //RISKMANAGER_H