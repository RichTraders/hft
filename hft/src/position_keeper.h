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

#ifndef POSITION_KEEPER_H
#define POSITION_KEEPER_H

#include <array>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "common/fixed_point_config.hpp"
#include "common/ini_config.hpp"
#include "common/logger.h"
#include "common/types.h"

namespace trading {
struct BBO;
struct ExecutionReport;

struct PositionInfo {
  // All values stored as int64 with qty_scale
  // position = qty (scaled by kQtyScale)
  // cost = price * qty (scaled by kPriceScale * kQtyScale)
  // pnl = price * qty (scaled by kPriceScale * kQtyScale)
  int64_t position_ = 0;
  int64_t long_position_raw_ = 0;
  int64_t short_position_raw_ = 0;
  int64_t long_cost_ = 0;      // price * qty scale
  int64_t short_cost_ = 0;     // price * qty scale
  int64_t long_real_pnl_ = 0;  // price * qty scale
  int64_t long_unreal_pnl_ = 0;
  int64_t short_real_pnl_ = 0;
  int64_t short_unreal_pnl_ = 0;
  int64_t real_pnl_ = 0;
  int64_t unreal_pnl_ = 0;
  int64_t total_pnl_ = 0;
  std::array<int64_t, common::sideToIndex(common::Side::kTrade)> open_vwap_{};
  int64_t volume_ = 0;

  [[nodiscard]] int64_t get_position() const noexcept { return position_; }
  [[nodiscard]] int64_t get_total_pnl() const noexcept { return total_pnl_; }

  [[nodiscard]] double get_position_double() const noexcept {
    return static_cast<double>(position_) / common::FixedPointConfig::kQtyScale;
  }
  [[nodiscard]] double get_total_pnl_double() const noexcept {
    return static_cast<double>(total_pnl_) /
           (common::FixedPointConfig::kPriceScale *
               common::FixedPointConfig::kQtyScale);
  }

  const BBO* bbo_ = nullptr;

  [[nodiscard]] std::string toString() const;

  void add_fill(const ExecutionReport* report,
      const common::Logger::Producer& logger) noexcept;

  void update_bbo(const BBO* bbo,
      const common::Logger::Producer& logger) noexcept;
};

class PositionKeeper {
 public:
  explicit PositionKeeper(const common::Logger::Producer& logger)
      : logger_(logger),
        ticker_position_{{INI_CONFIG.get("meta", "ticker"), PositionInfo{}}} {
    LOG_INFO(logger_, "[Constructor] PositionKeeper Created");
  }
  // NOLINTNEXTLINE(modernize-use-equals-default) - logs destruction
  ~PositionKeeper() {
    LOG_INFO(logger_, "[Destructor] PositionKeeper Destroy");
  }

  void add_fill(const ExecutionReport* report) noexcept;

  void update_bbo(const common::TickerId& ticker_id, const BBO* bbo) noexcept;

  [[nodiscard]] auto get_position_info(
      const common::TickerId& ticker_id) noexcept {
    return &(ticker_position_.at(ticker_id));
  }

  [[nodiscard]] std::string toString() const;

  PositionKeeper() = delete;

  PositionKeeper(const PositionKeeper&) = delete;

  PositionKeeper(const PositionKeeper&&) = delete;

  PositionKeeper& operator=(const PositionKeeper&) = delete;

  PositionKeeper& operator=(const PositionKeeper&&) = delete;

 private:
  const common::Logger::Producer& logger_;

  absl::flat_hash_map<std::string, PositionInfo> ticker_position_;
};
}  // namespace trading

#endif  //POSITION_KEEPER_H