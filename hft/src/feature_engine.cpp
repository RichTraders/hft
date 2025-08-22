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

#include "feature_engine.h"
#include "common/logger.h"
#include "core/NewOroFix44/market_data.h"
#include "order_book.h"

using common::Price;
using common::Side;
using common::TickerId;

constexpr int kLevel10 = 10;
namespace trading {

void FeatureEngine::on_order_book_updated(const Price price, const Side side,
                                          MarketOrderBook* book) noexcept {
  const auto* bbo = book->get_bbo();
  if (LIKELY(bbo->bid_price != common::kPriceInvalid &&
             bbo->ask_price != common::kPriceInvalid)) {
    mkt_price_ = (bbo->bid_price.value * bbo->ask_qty.value +
                  bbo->ask_price.value * bbo->bid_qty.value) /
                 (bbo->bid_qty.value + bbo->ask_qty.value);
    spread_ = bbo->ask_price.value - bbo->bid_price.value;

    auto bid_index = book->peek_levels(true, kLevel10);
    auto ask_index = book->peek_levels(true, kLevel10);
  }

  logger_->debug(
      std::format("[Updated] price:{} side:{} mkt-price:{} agg-trade-ratio:{}",
                  common::toString(price), common::toString(side), mkt_price_,
                  agg_trade_qty_ratio_));
}
void FeatureEngine::on_trade_updated(const MarketData* market_update,
                                     MarketOrderBook* book) noexcept {
  const auto* bbo = book->get_bbo();
  if (LIKELY(bbo->bid_price.value != common::kPriceInvalid &&
             bbo->ask_price.value != common::kPriceInvalid)) {
    agg_trade_qty_ratio_ =
        static_cast<double>(market_update->qty.value) /
        (market_update->side == Side::kBuy ? bbo->ask_qty.value
                                           : bbo->bid_qty.value);
  }

  const auto idx = static_cast<size_t>(vwap_index_ & (kVwapSize - 1));
  if (LIKELY(vwap_index_ >= kVwapSize)) {
    const double old_q = vwap_qty_[idx];
    const double old_p = vwap_price_[idx];
    acc_vwap_qty_ -= old_q;
    acc_vwap_ -= old_p * old_q;
  }
  vwap_price_[idx] = market_update->price.value;
  vwap_qty_[idx] = market_update->qty.value;
  acc_vwap_qty_ += vwap_qty_[idx];
  acc_vwap_ = std::fma(vwap_price_[idx], vwap_qty_[idx], acc_vwap_);
  if (LIKELY(acc_vwap_qty_ > 0.0)) {
    vwap_ = acc_vwap_ / acc_vwap_qty_;
  }
  vwap_index_++;

  logger_->debug(std::format("[Updated] {} mkt-price:{} agg-trade-ratio:{}",
                             market_update->toString(), mkt_price_,
                             agg_trade_qty_ratio_));
}

double FeatureEngine::vwap_from_levels(const std::vector<LevelView>& level) {
  double num = 0.0L;
  double den = 0.0L;
  const auto level_size = level.size();
  for (size_t index = 0; index < level_size; ++index) {
    num += static_cast<double>(level[index].price) * level[index].qty;
    den += static_cast<double>(level[index].qty);
  }
  return den > 0.0 ? (num / den) : common::kPriceInvalid;
}

double FeatureEngine::orderbook_imbalance_from_levels(
    const std::vector<double>& bid_levels,
    const std::vector<double>& ask_levels) {
  const size_t min_size = std::min(bid_levels.size(), ask_levels.size());

  long double total = 0.0L;  // sum(bid)+sum(ask)
  long double diff = 0.0L;   // sum(bid)-sum(ask)

  size_t index = 0;
  //loop unrolling
  for (; index + 3 < min_size; index += 4) {
    const double bid0 = bid_levels[index + 0];
    const double ask0 = ask_levels[index + 0];
    const double bid1 = bid_levels[index + 1];
    const double ask1 = ask_levels[index + 1];
    const double bid2 = bid_levels[index + 2];
    const double ask2 = ask_levels[index + 2];
    const double bid3 = bid_levels[index + 3];
    const double ask3 = ask_levels[index + 3];
    total += (bid0 + ask0) + (bid1 + ask1) + (bid2 + ask2) + (bid3 + ask3);
    diff += (bid0 - ask0) + (bid1 - ask1) + (bid2 - ask2) + (bid3 - ask3);
  }
  for (; index < min_size; ++index) {
    const double bid = bid_levels[index];
    const double ask = ask_levels[index];
    total += bid + ask;
    diff += bid - ask;
  }

  for (size_t j = min_size; j < bid_levels.size(); ++j) {
    total += bid_levels[j];
    diff += bid_levels[j];
  }
  for (size_t j = min_size; j < ask_levels.size(); ++j) {
    total += ask_levels[j];
    diff -= ask_levels[j];
  }

  if (total <= 0.0L)
    return 0.0;
  auto result = static_cast<double>(diff / total);
  if (result > 1.0)
    result = 1.0;
  else if (result < -1.0)
    result = -1.0;
  return result;
}
}  // namespace trading