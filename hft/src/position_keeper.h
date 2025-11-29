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

#include "common/ini_config.hpp"
#include "common/logger.h"
#include "common/types.h"

namespace trading {
struct BBO;
struct ExecutionReport;

struct PositionInfo {
  double position_ = 0;
  double real_pnl_ = 0;
  double unreal_pnl_ = 0;
  double total_pnl_ = 0;
  std::array<double, common::sideToIndex(common::Side::kTrade)> open_vwap_;
  common::Qty volume_ = common::Qty{0};
  const BBO* bbo_ = nullptr;

  [[nodiscard]] std::string toString() const;

  void add_fill(const ExecutionReport* report,
      common::Logger::Producer& logger) noexcept;

  void update_bbo(const BBO* bbo, common::Logger::Producer& logger) noexcept;
};

class PositionKeeper {
 public:
  explicit PositionKeeper(common::Logger* logger)
      : logger_(logger->make_producer()),
        ticker_position_{{INI_CONFIG.get("meta", "ticker"), PositionInfo{}}} {
    logger_.info("[Constructor] PositionKeeper Created");
  }
  ~PositionKeeper() { logger_.info("[Destructor] PositionKeeper Destroy"); }

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
  common::Logger::Producer logger_;

  std::unordered_map<std::string, PositionInfo> ticker_position_;
};
}  // namespace trading

#endif  //POSITION_KEEPER_H