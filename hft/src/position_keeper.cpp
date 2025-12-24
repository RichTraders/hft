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
using common::Qty;
using common::Side;
using common::sideToIndex;

namespace trading {
std::string PositionInfo::toString() const {
  std::ostringstream stream;
  const double vwap_buy =
      (position_ != 0.0)
          ? open_vwap_[sideToIndex(Side::kBuy)] / std::abs(position_)
          : 0.0;
  const double vwap_sell =
      (position_ != 0.0)
          ? open_vwap_[sideToIndex(Side::kSell)] / std::abs(position_)
          : 0.0;
  const double long_vwap =
      (long_position_ > 0) ? long_cost_ / long_position_ : 0.0;
  const double short_vwap =
      (short_position_ > 0) ? short_cost_ / short_position_ : 0.0;

  stream << "Position{" << "pos:" << position_ << " L[qty:" << long_position_
         << " vwap:" << long_vwap << " u:" << long_unreal_pnl_
         << " r:" << long_real_pnl_ << "]" << " S[qty:" << short_position_
         << " vwap:" << short_vwap << " u:" << short_unreal_pnl_
         << " r:" << short_real_pnl_ << "]" << " u-pnl:" << unreal_pnl_
         << " r-pnl:" << real_pnl_ << " t-pnl:" << total_pnl_
         << " vol:" << common::toString(volume_) << " vwaps:[" << vwap_buy
         << "X" << vwap_sell << "] " << (bbo_ ? bbo_->toString() : "") << "}";

  return stream.str();
}

void PositionInfo::add_fill(const ExecutionReport* report,
    const common::Logger::Producer& logger) noexcept {
  const auto old_position = position_;
  const auto idx = sideToIndex(report->side);
  const auto opp_side_index = oppIndex(idx);
  const int sgn = sideToValue(report->side);
  position_ += report->last_qty.value * static_cast<double>(sgn);
  volume_.value += report->last_qty.value;

  if (const auto* pos_side =
          report->position_side ? &(*report->position_side) : nullptr) {
    if (*pos_side == common::PositionSide::kLong) {
      if (report->side == Side::kBuy) {
        long_cost_ += report->price.value * report->last_qty.value;
        long_position_ += report->last_qty.value;
      } else {
        const double close_qty =
            std::min(report->last_qty.value, long_position_);
        if (long_position_ > 0 && close_qty > 0) {
          const double long_vwap = long_cost_ / long_position_;
          long_real_pnl_ += (report->price.value - long_vwap) * close_qty;
          long_cost_ -= long_vwap * close_qty;
        }
        long_position_ -= report->last_qty.value;
        if (long_position_ < 0) {
          long_position_ = 0;
          long_cost_ = 0;
        }
      }
    } else if (*pos_side == common::PositionSide::kShort) {
      if (report->side == Side::kSell) {
        short_cost_ += report->price.value * report->last_qty.value;
        short_position_ += report->last_qty.value;
      } else {
        const double close_qty =
            std::min(report->last_qty.value, short_position_);
        if (short_position_ > 0 && close_qty > 0) {
          const double short_vwap = short_cost_ / short_position_;
          short_real_pnl_ += (short_vwap - report->price.value) * close_qty;
          short_cost_ -= short_vwap * close_qty;
        }
        short_position_ -= report->last_qty.value;
        if (short_position_ < 0) {
          short_position_ = 0;
          short_cost_ = 0;
        }
      }
    }
  }

  // Net position tracking (for Spot or One-Way Mode without position_side)
  double net_real_pnl_delta = 0;
  if (old_position * sgn >= 0) {
    open_vwap_[idx] += report->price.value * report->last_qty.value;
  } else {
    const auto opp_side_vwap =
        open_vwap_[opp_side_index] / std::abs(old_position);
    open_vwap_[opp_side_index] = opp_side_vwap * std::abs(position_);
    net_real_pnl_delta =
        std::min(report->last_qty.value, std::abs(old_position)) *
        (opp_side_vwap - report->price.value) * sgn;
    if (position_ * old_position < 0) {
      open_vwap_[idx] = (report->price.value * std::abs(position_));
      open_vwap_[opp_side_index] = 0;
    }
  }

  if (position_ == 0.) {
    open_vwap_[0] = open_vwap_[1] = 0.;
    unreal_pnl_ = 0.;
  } else {
    if (position_ > 0.)
      unreal_pnl_ = (report->price.value - open_vwap_[sideToIndex(Side::kBuy)] /
                                               std::abs(position_)) *
                    std::abs(position_);
    else
      unreal_pnl_ =
          (open_vwap_[sideToIndex(Side::kSell)] / std::abs(position_) -
              report->price.value) *
          std::abs(position_);
  }

  // Long/Short unrealized PnL
  if (long_position_ > 0) {
    const double long_vwap = long_cost_ / long_position_;
    long_unreal_pnl_ = (report->price.value - long_vwap) * long_position_;
  } else {
    long_unreal_pnl_ = 0;
  }
  if (short_position_ > 0) {
    const double short_vwap = short_cost_ / short_position_;
    short_unreal_pnl_ = (short_vwap - report->price.value) * short_position_;
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

  logger.info("[PositionInfo][Fill] {}", toString());
}

void PositionInfo::update_bbo(const BBO* bbo,
    const common::Logger::Producer& logger) noexcept {
  bbo_ = bbo;

  if (bbo->bid_price == common::kPriceInvalid ||
      bbo->ask_price == common::kPriceInvalid) {
    return;
  }

  const auto mid_price = (bbo->bid_price.value + bbo->ask_price.value) * 0.5;

  // Net position unrealized PnL
  if (position_ != 0) {
    if (position_ > 0)
      unreal_pnl_ = (mid_price - open_vwap_[sideToIndex(Side::kBuy)] /
                                     std::abs(position_)) *
                    std::abs(position_);
    else
      unreal_pnl_ =
          (open_vwap_[sideToIndex(Side::kSell)] / std::abs(position_) -
              mid_price) *
          std::abs(position_);
  }

  // Long/Short unrealized PnL
  if (long_position_ > 0) {
    const double long_vwap = long_cost_ / long_position_;
    long_unreal_pnl_ = (mid_price - long_vwap) * long_position_;
  } else {
    long_unreal_pnl_ = 0;
  }
  if (short_position_ > 0) {
    const double short_vwap = short_cost_ / short_position_;
    short_unreal_pnl_ = (short_vwap - mid_price) * short_position_;
  } else {
    short_unreal_pnl_ = 0;
  }

  const auto old_total_pnl = total_pnl_;
  total_pnl_ = unreal_pnl_ + real_pnl_;

  if (total_pnl_ != old_total_pnl)
    logger.info("[PositionInfo][Updated] {} {}", toString(), bbo_->toString());
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
  Qty total_vol = Qty{0};

  std::ostringstream stream;
  for (const auto& position : ticker_position_) {
    const auto& name = position.first;
    const auto& info = position.second;
    stream << "TickerId:" << name << " " << info.toString() << "\n";

    total_pnl += info.total_pnl_;
    total_vol += info.volume_;
  }
  stream << "Total PnL:" << total_pnl << " Vol:" << total_vol.value << "\n";

  return stream.str();
}
}  // namespace trading