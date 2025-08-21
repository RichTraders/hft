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

#include "position_keeper.h"

#include "order_book.h"
#include "order_entry.h"

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

  stream << "Position{" << "pos:" << position_ << " u-pnl:" << unreal_pnl_
         << " r-pnl:" << real_pnl_ << " t-pnl:" << total_pnl_
         << " vol:" << common::toString(volume_) << " vwaps:[" << " vwaps:["
         << vwap_buy << "X" << vwap_sell << "] "
         << (bbo_ ? bbo_->toString() : "") << "}";

  return stream.str();
}

void PositionInfo::add_fill(const ExecutionReport* report,
                            common::Logger* logger) noexcept {
  const auto old_position = position_;
  const auto idx = sideToIndex(report->side);
  const auto opp_side_index = oppIndex(idx);
  const int sgn = sideToValue(report->side);
  position_ += report->last_qty.value * static_cast<double>(sgn);
  volume_.value += report->last_qty.value;

  if (old_position * sgn >= 0) {
    open_vwap_[idx] += report->price.value * report->last_qty.value;
  } else {
    const auto opp_side_vwap =
        open_vwap_[opp_side_index] / std::abs(old_position);
    open_vwap_[opp_side_index] = opp_side_vwap * std::abs(position_);
    real_pnl_ += std::min(report->last_qty.value, std::abs(old_position)) *
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

  total_pnl_ = unreal_pnl_ + real_pnl_;

  logger->info(std::format("{} {}", toString(), report->toString()));
}

void PositionInfo::update_bbo(const BBO* bbo, common::Logger* logger) noexcept {
  bbo_ = bbo;

  if (position_ != 0 && bbo->bid_price != common::kPriceInvalid &&
      bbo->ask_price != common::kPriceInvalid) {
    const auto mid_price = (bbo->bid_price.value + bbo->ask_price.value) * 0.5;
    if (position_ > 0)
      unreal_pnl_ = (mid_price - open_vwap_[sideToIndex(Side::kBuy)] /
                                     std::abs(position_)) *
                    std::abs(position_);
    else
      unreal_pnl_ =
          (open_vwap_[sideToIndex(Side::kSell)] / std::abs(position_) -
           mid_price) *
          std::abs(position_);

    const auto old_total_pnl = total_pnl_;
    total_pnl_ = unreal_pnl_ + real_pnl_;

    if (total_pnl_ != old_total_pnl)
      logger->info(std::format("{} {}", toString(), bbo_->toString()));
  }
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