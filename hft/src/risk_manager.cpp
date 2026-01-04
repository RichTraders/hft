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

#include <string>

#include "ini_config.hpp"
#include "logger.h"
#include "risk_manager.h"

namespace trading {

RiskCheckResult RiskInfo::checkPreTradeRisk(const common::Side side,
    const common::QtyType qty, common::QtyType reserved_position,
    const common::Logger::Producer& logger) noexcept {
  if (qty.value > risk_cfg_.max_order_size_.value) {
    LOG_DEBUG(logger,
        "[Risk]Order is too large [Desired:{}][Allow:{}]",
        qty.value,
        risk_cfg_.max_order_size_.value);
    return RiskCheckResult::kOrderTooLarge;
  }
  const int64_t current_position = position_info_->get_position();
  const int64_t current_total_pnl = position_info_->get_total_pnl();

  if (current_position + reserved_position.value +
          sideToValue(side) * qty.value >
      risk_cfg_.max_position_.value) {
    LOG_DEBUG(logger,
        "[Risk]Maximum position allowed has been reached."
        "[Desired:{}][Current:{}][Working:{}][Allow:{}]",
        sideToValue(side) * qty.value,
        current_position,
        reserved_position.value,
        risk_cfg_.max_position_.value);
    return RiskCheckResult::kPositionTooLarge;
  }
  if (current_position + reserved_position.value +
          sideToValue(side) * qty.value <
      risk_cfg_.min_position_.value) {
    LOG_DEBUG(logger,
        "[Risk]Minimum position allowed has been reached."
        "[Desired:{}][Current:{}][Working:{}][Allow:{}]",
        sideToValue(side) * qty.value,
        current_position,
        reserved_position.value,
        risk_cfg_.min_position_.value);
    return RiskCheckResult::kPositionTooSmall;
  }
  if (current_total_pnl < risk_cfg_.max_loss_) {
    LOG_DEBUG(logger,
        "[Risk]Maximum PnL allowed has been reached."
        "[Current:{}][Allow:{}]",
        current_total_pnl,
        risk_cfg_.max_loss_);
    return RiskCheckResult::kLossTooLarge;
  }

  return RiskCheckResult::kAllowed;
}

RiskManager::RiskManager(const common::Logger::Producer& logger,
    PositionKeeper* position_keeper,
    const common::TradeEngineCfgHashMap& ticker_cfg)
    : logger_(logger) {
  const std::string ticker = INI_CONFIG.get("meta", "ticker");
  ticker_risk_[INI_CONFIG.get("meta", "ticker")] =
      RiskInfo(position_keeper->get_position_info(ticker),
          ticker_cfg.at(ticker).risk_cfg_);
  LOG_INFO(logger_, "[Constructor] RiskManager Created");
}

RiskManager::~RiskManager() {
  LOG_INFO(logger_, "[Destructor] RiskManager Destroy");
}
}  // namespace trading