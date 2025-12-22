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
#pragma once

#include "common/logger.h"
#include "common/types.h"
#include "inventory_model.h"
#include "position_keeper.h"

namespace trading {

class InventoryManager {
 public:
  InventoryManager(const common::Logger::Producer& logger,
      PositionKeeper* position_keeper,
      const common::TradeEngineCfgHashMap& ticker_cfg);

  ~InventoryManager();

  InventoryManager(const InventoryManager&) = delete;
  InventoryManager& operator=(const InventoryManager&) = delete;
  InventoryManager(InventoryManager&&) = delete;
  InventoryManager& operator=(InventoryManager&&) = delete;

  [[nodiscard]] auto get_quote_adjustment(common::Side side,
      const common::TickerId& ticker_id) const noexcept -> double;

  [[nodiscard]] auto get_skew_coefficient() const noexcept -> double;
  void set_skew_coefficient(double coefficient) noexcept;

 private:
  static constexpr double kModelDefaultParameter = 0.001;
  const common::Logger::Producer& logger_;
  PositionKeeper* position_keeper_;
  LinearSkewModel model_;
  double target_position_;
};

}  // namespace trading
