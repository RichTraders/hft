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

#ifndef FEATURE_ENGINE_HPP
#define FEATURE_ENGINE_HPP

#include <cmath>
#include <vector>

#include "common/logger.h"
#include "common/types.h"
#include "core/market_data.h"
#include "order_book.hpp"

namespace trading {
template <typename Strategy>
class FeatureEngine {
 public:
  explicit FeatureEngine(const common::Logger::Producer& logger)
      : logger_(logger),
        vwap_size_(INI_CONFIG.get_int("strategy", "vwap_size", kVwapSize)),
        vwap_qty_(vwap_size_),
        vwap_price_(vwap_size_) {
    logger_.info("[Constructor] FeatureEngine Created");
  }

  ~FeatureEngine() { logger_.info("[Destructor] FeatureEngine Destroy"); }

  auto on_trade_updated(const MarketData* market_update,
      MarketOrderBook<Strategy>* book) noexcept -> void {
    const auto* bbo = book->get_bbo();
    if (LIKELY(bbo->bid_price.value != common::kPriceInvalid &&
               bbo->ask_price.value != common::kPriceInvalid)) {
      agg_trade_qty_ratio_ =
          static_cast<double>(market_update->qty.value) /
          (market_update->side == common::Side::kBuy ? bbo->ask_qty.value
                                                     : bbo->bid_qty.value);
    }

    const auto idx = static_cast<size_t>(vwap_index_ & (vwap_size_ - 1));
    if (LIKELY(vwap_index_ >= vwap_size_)) {
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

    logger_.trace("[Updated] {} mkt-price:{} agg-trade-ratio:{}",
        market_update->toString(),
        mkt_price_,
        agg_trade_qty_ratio_);
  }

  auto on_book_ticker_updated(
      const MarketData* market_update) noexcept -> void {
    if (market_update->side == common::Side::kBuy) {
      book_ticker_.bid_price = market_update->price.value;
      book_ticker_.bid_qty = market_update->qty.value;
    } else {
      book_ticker_.ask_price = market_update->price.value;
      book_ticker_.ask_qty = market_update->qty.value;
    }
  }

  auto on_order_book_updated(common::Price price, common::Side side,
      MarketOrderBook<Strategy>* book) noexcept -> void {
    const auto* bbo = book->get_bbo();
    if (LIKELY(bbo->bid_price != common::kPriceInvalid &&
               bbo->ask_price != common::kPriceInvalid)) {
      mkt_price_ = (bbo->bid_price.value * bbo->ask_qty.value +
                       bbo->ask_price.value * bbo->bid_qty.value) /
                   (bbo->bid_qty.value + bbo->ask_qty.value);
      spread_ = bbo->ask_price.value - bbo->bid_price.value;

      auto bid_index = book->peek_levels(true, kLevel10);
      auto ask_index = book->peek_levels(false, kLevel10);
    }

    logger_.trace("[Updated] price:{} side:{} mkt-price:{} agg-trade-ratio:{}",
        common::toString(price),
        common::toString(side),
        mkt_price_,
        agg_trade_qty_ratio_);
  }

  static double vwap_from_levels(const std::vector<LevelView>& level) {
    double num = 0.0L;
    double den = 0.0L;
    const auto level_size = level.size();
    for (size_t index = 0; index < level_size; ++index) {
      num += static_cast<double>(level[index].price) * level[index].qty;
      den += static_cast<double>(level[index].qty);
    }
    return den > 0.0 ? (num / den) : common::kPriceInvalid;
  }

  static double orderbook_imbalance_from_levels(
      const std::vector<double>& bid_levels,
      const std::vector<double>& ask_levels) {
    const size_t min_size = std::min(bid_levels.size(), ask_levels.size());

    long double total = 0.0L;  // sum(bid)+sum(ask)
    long double diff = 0.0L;   // sum(bid)-sum(ask)

    size_t index = 0;
    // loop unrolling
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

  [[nodiscard]] auto get_market_price() const noexcept { return mkt_price_; }
  [[nodiscard]] auto get_mid_price() const noexcept {
    return (book_ticker_.bid_price + book_ticker_.ask_price) * kMidPriceFactor;
  }
  [[nodiscard]] auto get_spread() const noexcept { return spread_; }
  [[nodiscard]] auto get_spread_fast() const noexcept {
    return book_ticker_.ask_price - book_ticker_.bid_price;
  }
  [[nodiscard]] auto get_vwap() const noexcept { return vwap_; }

  [[nodiscard]] auto get_agg_trade_qty_ratio() const noexcept {
    return agg_trade_qty_ratio_;
  }

  FeatureEngine() = delete;

  FeatureEngine(const FeatureEngine&) = delete;

  FeatureEngine(const FeatureEngine&&) = delete;

  FeatureEngine& operator=(const FeatureEngine&) = delete;

  FeatureEngine& operator=(const FeatureEngine&&) = delete;

 private:
  static constexpr int kLevel10 = 10;
  static constexpr int kVwapSize = 64;
  static constexpr double kMidPriceFactor = 0.5;
  const common::Logger::Producer& logger_;
  double mkt_price_ = common::kPriceInvalid;
  double agg_trade_qty_ratio_ = common::kQtyInvalid;
  double spread_ = common::kPriceInvalid;
  double acc_vwap_qty_ = 0.;
  double acc_vwap_ = 0.;
  double vwap_ = 0.;
  const uint32_t vwap_size_ = 0;
  std::vector<double> vwap_qty_;
  std::vector<double> vwap_price_;
  uint32_t vwap_index_ = 0;
  struct BookTicker {
    double bid_price;
    double bid_qty;
    double ask_price;
    double ask_qty;
  } book_ticker_;
};
}  // namespace trading

#endif  // FEATURE_ENGINE_HPP
