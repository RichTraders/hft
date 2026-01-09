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

#include <algorithm>
#include <cmath>
#include <optional>
#include <sstream>
#include <string>

#include "order_book.hpp"
#include "order_entry.h"
#include "position_keeper.h"

using common::oppIndex;
using common::Side;
using common::sideToIndex;

namespace trading {

std::string PositionInfo::toString() const {
  std::ostringstream stream;
  constexpr double kPriceScale = common::FixedPointConfig::kPriceScale;
  constexpr double kQtyScale = common::FixedPointConfig::kQtyScale;
  constexpr double kPQScale = kPriceScale * kQtyScale;

  const double pos = static_cast<double>(position_) / kQtyScale;
  const double long_pos = static_cast<double>(long_position_raw_) / kQtyScale;
  const double short_pos = static_cast<double>(short_position_raw_) / kQtyScale;
  const double vol = static_cast<double>(volume_) / kQtyScale;

  // vwap = cost / position (cost is price*qty scale, position is qty scale)
  // so vwap = (cost / kPQScale) / (position / kQtyScale) = cost / (position * kPriceScale)
  const double vwap_buy =
      (position_ != 0)
          ? static_cast<double>(open_vwap_[sideToIndex(Side::kBuy)]) /
                (static_cast<double>(std::abs(position_)) * kPriceScale)
          : 0.0;
  const double vwap_sell =
      (position_ != 0)
          ? static_cast<double>(open_vwap_[sideToIndex(Side::kSell)]) /
                (static_cast<double>(std::abs(position_)) * kPriceScale)
          : 0.0;
  const double long_vwap =
      (long_position_raw_ > 0)
          ? static_cast<double>(long_cost_) /
                (static_cast<double>(long_position_raw_) * kPriceScale)
          : 0.0;
  const double short_vwap =
      (short_position_raw_ > 0)
          ? static_cast<double>(short_cost_) /
                (static_cast<double>(short_position_raw_) * kPriceScale)
          : 0.0;

  stream << "Position{" << "pos:" << pos << " L[qty:" << long_pos
         << " vwap:" << long_vwap
         << " u:" << static_cast<double>(long_unreal_pnl_) / kPQScale
         << " r:" << static_cast<double>(long_real_pnl_) / kPQScale << "]"
         << " S[qty:" << short_pos << " vwap:" << short_vwap
         << " u:" << static_cast<double>(short_unreal_pnl_) / kPQScale
         << " r:" << static_cast<double>(short_real_pnl_) / kPQScale << "]"
         << " u-pnl:" << static_cast<double>(unreal_pnl_) / kPQScale
         << " r-pnl:" << static_cast<double>(real_pnl_) / kPQScale
         << " t-pnl:" << static_cast<double>(total_pnl_) / kPQScale
         << " vol:" << vol << " vwaps:[" << vwap_buy << "X" << vwap_sell << "] "
         << (bbo_ ? bbo_->toString() : "") << "}";

  return stream.str();
}

void PositionInfo::add_fill(const ExecutionReport* report,
    const common::Logger::Producer& logger) noexcept {
  const auto old_position = position_;
  const auto idx = sideToIndex(report->side);
  const auto opp_side_index = oppIndex(idx);
  const int64_t sgn = sideToValue(report->side);

  const int64_t last_qty = report->last_qty.value;
  const int64_t price = report->price.value;

  position_ += last_qty * sgn;
  volume_ += last_qty;

  if (const auto* pos_side =
          report->position_side ? &(*report->position_side) : nullptr) {
    if (*pos_side == common::PositionSide::kLong) {
      if (report->side == Side::kBuy) {
        long_cost_ += price * last_qty;
        long_position_raw_ += last_qty;
      } else {
        const int64_t close_qty = std::min(last_qty, long_position_raw_);
        if (long_position_raw_ > 0 && close_qty > 0) {
          // long_vwap = long_cost / long_position (in price scale)
          const int64_t long_vwap = long_cost_ / long_position_raw_;
          long_real_pnl_ += (price - long_vwap) * close_qty;
          long_cost_ -= long_vwap * close_qty;
        }
        long_position_raw_ -= last_qty;
        if (long_position_raw_ < 0) {
          long_position_raw_ = 0;
          long_cost_ = 0;
        }
      }
    } else if (*pos_side == common::PositionSide::kShort) {
      if (report->side == Side::kSell) {
        short_cost_ += price * last_qty;
        short_position_raw_ += last_qty;
      } else {
        const int64_t close_qty = std::min(last_qty, short_position_raw_);
        if (short_position_raw_ > 0 && close_qty > 0) {
          const int64_t short_vwap = short_cost_ / short_position_raw_;
          short_real_pnl_ += (short_vwap - price) * close_qty;
          short_cost_ -= short_vwap * close_qty;
        }
        short_position_raw_ -= last_qty;
        if (short_position_raw_ < 0) {
          short_position_raw_ = 0;
          short_cost_ = 0;
        }
      }
    }
  }

  // Net position tracking (for Spot or One-Way Mode without position_side)
  int64_t net_real_pnl_delta = 0;
  if (old_position * sgn >= 0) {
    open_vwap_[idx] += price * last_qty;
  } else {
    const int64_t opp_side_vwap =
        open_vwap_[opp_side_index] / std::abs(old_position);
    open_vwap_[opp_side_index] = opp_side_vwap * std::abs(position_);
    net_real_pnl_delta = std::min(last_qty, std::abs(old_position)) *
                         (opp_side_vwap - price) * sgn;
    if (position_ * old_position < 0) {
      open_vwap_[idx] = price * std::abs(position_);
      open_vwap_[opp_side_index] = 0;
    }
  }

  if (position_ == 0) {
    open_vwap_[0] = open_vwap_[1] = 0;
    unreal_pnl_ = 0;
  } else {
    if (position_ > 0) {
      // unreal = (price - vwap) * position
      // vwap = open_vwap / abs(position)
      // unreal = price * position - open_vwap
      unreal_pnl_ =
          price * std::abs(position_) - open_vwap_[sideToIndex(Side::kBuy)];
    } else {
      unreal_pnl_ =
          open_vwap_[sideToIndex(Side::kSell)] - price * std::abs(position_);
    }
  }

  // Long/Short unrealized PnL
  if (long_position_raw_ > 0) {
    const int64_t long_vwap = long_cost_ / long_position_raw_;
    long_unreal_pnl_ = (price - long_vwap) * long_position_raw_;
  } else {
    long_unreal_pnl_ = 0;
  }
  if (short_position_raw_ > 0) {
    const int64_t short_vwap = short_cost_ / short_position_raw_;
    short_unreal_pnl_ = (short_vwap - price) * short_position_raw_;
  } else {
    short_unreal_pnl_ = 0;
  }

  // Use Long/Short PnL if position_side is present, otherwise use net PnL
  if (report->position_side) {
    real_pnl_ = long_real_pnl_ + short_real_pnl_;
  } else {
    real_pnl_ += net_real_pnl_delta;
  }
  total_pnl_ = unreal_pnl_ + real_pnl_;

  LOG_INFO(logger, "[PositionInfo][Fill] {}", toString());
}

void PositionInfo::update_bbo(const BBO* bbo,
    const common::Logger::Producer& logger) noexcept {
  bbo_ = bbo;

  if (bbo->bid_price.value <= 0 || bbo->ask_price.value <= 0) {
    return;
  }

  const int64_t mid_price = (bbo->bid_price.value + bbo->ask_price.value) / 2;

  // Net position unrealized PnL
  if (position_ != 0) {
    if (position_ > 0) {
      unreal_pnl_ =
          mid_price * std::abs(position_) - open_vwap_[sideToIndex(Side::kBuy)];
    } else {
      unreal_pnl_ = open_vwap_[sideToIndex(Side::kSell)] -
                    mid_price * std::abs(position_);
    }
  }

  // Long/Short unrealized PnL
  if (long_position_raw_ > 0) {
    const int64_t long_vwap = long_cost_ / long_position_raw_;
    long_unreal_pnl_ = (mid_price - long_vwap) * long_position_raw_;
  } else {
    long_unreal_pnl_ = 0;
  }
  if (short_position_raw_ > 0) {
    const int64_t short_vwap = short_cost_ / short_position_raw_;
    short_unreal_pnl_ = (short_vwap - mid_price) * short_position_raw_;
  } else {
    short_unreal_pnl_ = 0;
  }

  const auto old_total_pnl = total_pnl_;
  total_pnl_ = unreal_pnl_ + real_pnl_;

  if (total_pnl_ != old_total_pnl)
    LOG_INFO(logger,
        "[PositionInfo][Updated] {} {}",
        toString(),
        bbo_->toString());
}

void PositionKeeper::add_fill(const ExecutionReport* report) noexcept {
  ticker_position_.at(report->symbol).add_fill(report, logger_);
}

void PositionKeeper::update_bbo(const common::TickerId& ticker_id,
    const BBO* bbo) noexcept {
  ticker_position_.at(ticker_id).update_bbo(bbo, logger_);
}

std::string PositionKeeper::toString() const {
  double total_pnl = 0;
  double total_vol = 0;

  std::ostringstream stream;
  for (const auto& position : ticker_position_) {
    const auto& name = position.first;
    const auto& info = position.second;
    stream << "TickerId:" << name << " " << info.toString() << "\n";

    total_pnl += info.get_total_pnl_double();
    total_vol +=
        static_cast<double>(info.volume_) / common::FixedPointConfig::kQtyScale;
  }
  stream << "Total PnL:" << total_pnl << " Vol:" << total_vol << "\n";

  return stream.str();
}
}  // namespace trading
