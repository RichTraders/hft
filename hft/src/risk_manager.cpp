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

#include "risk_manager.h"
#include "ini_config.hpp"
#include "logger.h"

namespace trading {
RiskCheckResult RiskInfo::checkPreTradeRisk(
    const common::Side side, const common::Qty qty,
    common::Qty reserved_position, common::Logger::Producer& logger) noexcept {
  if (qty.value > risk_cfg_.max_order_size_.value) {
    logger.info(std::format("[Risk]Order is too large [Desired:{}][Allow:{}]",
                            qty.value, risk_cfg_.max_order_size_.value));
    return RiskCheckResult::kOrderTooLarge;
  }
  if (position_info_->position_ + reserved_position.value +
          sideToValue(side) * qty.value >
      risk_cfg_.max_position_.value) {
    logger.info(
        std::format("[Risk]Maximum position allowed has been reached."
                    "[Desired:{}][Current:{}][Working:{}][Allow:{}]",
                    sideToValue(side) * qty.value, position_info_->position_,
                    reserved_position.value, risk_cfg_.max_position_.value));
    return RiskCheckResult::kPositionTooLarge;
  }
  if (position_info_->position_ + reserved_position.value +
          sideToValue(side) * qty.value <
      risk_cfg_.min_position_.value) {
    logger.info(
        std::format("[Risk]Minimum position allowed has been reached."
                    "[Desired:{}][Current:{}][Working:{}][Allow:{}]",
                    sideToValue(side) * qty.value, position_info_->position_,
                    reserved_position.value, risk_cfg_.min_position_.value));
    return RiskCheckResult::kPositionTooSmall;
  }
  if (position_info_->total_pnl_ < risk_cfg_.max_loss_) {
    logger.info(
        std::format("[Risk]Maximum PnL allowed has been reached."
                    "[Current:{}][Allow:{}]",
                    position_info_->total_pnl_, risk_cfg_.max_loss_));
    return RiskCheckResult::kLossTooLarge;
  }

  return RiskCheckResult::kAllowed;
}

RiskManager::RiskManager(common::Logger* logger,
                         PositionKeeper* position_keeper,
                         const common::TradeEngineCfgHashMap& ticker_cfg)
    : logger_(logger->make_producer()) {
  const std::string ticker = INI_CONFIG.get("meta", "ticker");
  ticker_risk_[INI_CONFIG.get("meta", "ticker")] =
      RiskInfo(position_keeper->get_position_info(ticker),
               ticker_cfg.at(ticker).risk_cfg_);
  logger_.info("[Constructor] RiskManager Created");
}

RiskManager::~RiskManager() {
  logger_.info("[Destructor] RiskManager Destroy");
}
}  // namespace trading