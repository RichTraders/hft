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

#include "inventory_manager.h"

#include "ini_config.hpp"

namespace trading {

InventoryManager::InventoryManager(const common::Logger::Producer& logger,
    PositionKeeper* position_keeper, const common::TradeEngineCfgHashMap&)
    : logger_(logger),
      position_keeper_(position_keeper),
      model_(INI_CONFIG.get_double("inventory", "skew_coefficient",
          kModelDefaultParameter)),
      target_position_(
          INI_CONFIG.get_double("inventory", "target_position", 0.0)) {
  logger_.info(
      std::format("InventoryManager initialized with skew_coefficient={}, "
                  "target_position={}",
          model_.get_skew_coefficient(),
          target_position_));
}

InventoryManager::~InventoryManager() {
  logger_.info("InventoryManager destroyed");
}

auto InventoryManager::get_quote_adjustment(common::Side side,
    const common::TickerId& ticker_id) const noexcept -> double {
  const auto* position_info = position_keeper_->get_position_info(ticker_id);
  const double current_position = position_info->position_;

  return model_.calculate_quote_adjustment(side,
      current_position,
      target_position_);
}

auto InventoryManager::get_skew_coefficient() const noexcept -> double {
  return model_.get_skew_coefficient();
}

void InventoryManager::set_skew_coefficient(double coefficient) noexcept {
  logger_.info(std::format("Updating skew_coefficient from {} to {}",
      model_.get_skew_coefficient(),
      coefficient));
  model_.set_skew_coefficient(coefficient);
}

}  // namespace trading
