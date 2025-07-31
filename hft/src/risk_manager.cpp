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
#include "logger.h"

namespace trading {
RiskManager::RiskManager(common::Logger* logger,
                         const PositionKeeper* position_keeper,
                         const common::TradeEngineCfgHashMap& ticker_cfg)
    : logger_(logger) {
  const std::string ticker = "BTCUSDT";
  ticker_risk_["BTCUSDT"] = RiskInfo(position_keeper->get_position_info(ticker),
                                     ticker_cfg.at(ticker).risk_cfg_);
  logger_->info("RiskManager Created");
}
}  // namespace trading