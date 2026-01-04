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

#include "common/fixed_point_config.hpp"
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
        vwap_qty_raw_(vwap_size_),
        vwap_price_raw_(vwap_size_) {
    LOG_INFO(logger_, "[Constructor] FeatureEngine Created");
  }

  ~FeatureEngine() { std::cout << "[Destructor] FeatureEngine Destroy\n"; }

  auto on_trade_updated(const MarketData* market_update,
      MarketOrderBook<Strategy>* book) noexcept -> void {
    const auto* bbo = book->get_bbo();
    if (LIKELY(bbo->bid_price.value > 0 && bbo->ask_price.value > 0)) {
      agg_trade_qty_ratio_ =
          static_cast<double>(market_update->qty.value) /
          (market_update->side == common::Side::kBuy ? bbo->ask_qty.value
                                                     : bbo->bid_qty.value);
    }

    const auto idx = static_cast<size_t>(vwap_index_ & (vwap_size_ - 1));
    if (LIKELY(vwap_index_ >= vwap_size_)) {
      const int64_t old_q = vwap_qty_raw_[idx];
      const int64_t old_p = vwap_price_raw_[idx];
      acc_vwap_qty_raw_ -= old_q;
      acc_vwap_raw_ -= old_p * old_q;
    }
    vwap_price_raw_[idx] = market_update->price.value;
    vwap_qty_raw_[idx] = market_update->qty.value;
    acc_vwap_qty_raw_ += vwap_qty_raw_[idx];
    acc_vwap_raw_ += vwap_price_raw_[idx] * vwap_qty_raw_[idx];
    if (LIKELY(acc_vwap_qty_raw_ > 0)) {
      // vwap_raw_ has unit: (price_scale * qty_scale) / qty_scale = price_scale
      vwap_raw_ = acc_vwap_raw_ / acc_vwap_qty_raw_;
    }
    vwap_index_++;

    LOG_TRACE(logger_,
        "[Updated] {} mkt-price:{} agg-trade-ratio:{}",
        market_update->toString(),
        get_market_price_double(),
        agg_trade_qty_ratio_);
  }

  auto on_book_ticker_updated(
      const MarketData* market_update) noexcept -> void {
    if (market_update->side == common::Side::kBuy) {
      book_ticker_raw_.bid_price = market_update->price.value;
      book_ticker_raw_.bid_qty = market_update->qty.value;
    } else {
      book_ticker_raw_.ask_price = market_update->price.value;
      book_ticker_raw_.ask_qty = market_update->qty.value;
    }
  }

  auto on_order_book_updated(common::PriceType price, common::Side side,
      MarketOrderBook<Strategy>* book) noexcept -> void {
    const auto* bbo = book->get_bbo();
    if (LIKELY(bbo->bid_price.value > 0 && bbo->ask_price.value > 0)) {
      // mkt_price = (bid_price * ask_qty + ask_price * bid_qty) / (bid_qty + ask_qty)

      const int64_t num = bbo->bid_price.value * bbo->ask_qty.value +
                          bbo->ask_price.value * bbo->bid_qty.value;
      const int64_t den = bbo->bid_qty.value + bbo->ask_qty.value;
      if (den > 0) {
        mkt_price_raw_ = num / den;
      }
      spread_raw_ = bbo->ask_price.value - bbo->bid_price.value;
    }

    LOG_TRACE(logger_,
        "[Updated] price:{} side:{} mkt-price:{} agg-trade-ratio:{}",
        common::toString(price),
        common::toString(side),
        get_market_price_double(),
        agg_trade_qty_ratio_);
  }

  static double vwap_from_levels(const std::vector<LevelView>& level) {
    int64_t num = 0;
    int64_t den = 0;
    const auto level_size = level.size();
    for (size_t index = 0; index < level_size; ++index) {
      num += level[index].price_raw * level[index].qty_raw;
      den += level[index].qty_raw;
    }
    if (den <= 0)
      return common::kPriceInvalid;
    // Result is in price_scale (price*qty/qty = price)
    // NOLINTNEXTLINE(bugprone-integer-division) - intentional integer division for VWAP
    return static_cast<double>(num / den) /
           common::FixedPointConfig::kPriceScale;
  }

  // OBI range: [-kObiScale, +kObiScale] representing [-1.0, +1.0]
  static constexpr int64_t kObiScale = 10000;
  [[nodiscard]] int64_t orderbook_imbalance_int64(
      const std::vector<int64_t>& bid_levels,
      const std::vector<int64_t>& ask_levels) const {
    const size_t min_size = std::min(bid_levels.size(), ask_levels.size());

    int64_t total = 0;
    int64_t diff = 0;

    size_t index = 0;

    for (; index + 3 < min_size; index += 4) {
      const int64_t bid0 = bid_levels[index + 0];
      const int64_t ask0 = ask_levels[index + 0];
      const int64_t bid1 = bid_levels[index + 1];
      const int64_t ask1 = ask_levels[index + 1];
      const int64_t bid2 = bid_levels[index + 2];
      const int64_t ask2 = ask_levels[index + 2];
      const int64_t bid3 = bid_levels[index + 3];
      const int64_t ask3 = ask_levels[index + 3];
      total += (bid0 + ask0) + (bid1 + ask1) + (bid2 + ask2) + (bid3 + ask3);
      diff += (bid0 - ask0) + (bid1 - ask1) + (bid2 - ask2) + (bid3 - ask3);
    }
    for (; index < min_size; ++index) {
      const int64_t bid = bid_levels[index];
      const int64_t ask = ask_levels[index];
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

    if (total <= 0)
      return 0;
    return (diff * kObiScale) / total;
  }

  [[nodiscard]] int64_t get_market_price() const noexcept {
    return mkt_price_raw_;
  }
  [[nodiscard]] int64_t get_mid_price() const noexcept {
    return (book_ticker_raw_.bid_price + book_ticker_raw_.ask_price) / 2;
  }
  [[nodiscard]] int64_t get_spread() const noexcept { return spread_raw_; }
  [[nodiscard]] int64_t get_spread_fast() const noexcept {
    return book_ticker_raw_.ask_price - book_ticker_raw_.bid_price;
  }
  [[nodiscard]] int64_t get_vwap() const noexcept { return vwap_raw_; }

  [[nodiscard]] double get_market_price_double() const noexcept {
    return static_cast<double>(mkt_price_raw_) /
           common::FixedPointConfig::kPriceScale;
  }

  [[nodiscard]] auto get_agg_trade_qty_ratio() const noexcept {
    return agg_trade_qty_ratio_;
  }

  FeatureEngine() = delete;

  FeatureEngine(const FeatureEngine&) = delete;

  FeatureEngine(const FeatureEngine&&) = delete;

  FeatureEngine& operator=(const FeatureEngine&) = delete;

  FeatureEngine& operator=(const FeatureEngine&&) = delete;

 private:
  static constexpr int kVwapSize = 64;
  const common::Logger::Producer& logger_;
  double agg_trade_qty_ratio_ = common::kQtyInvalid;
  const uint32_t vwap_size_ = 0;
  uint32_t vwap_index_ = 0;

  int64_t mkt_price_raw_ = 0;
  int64_t spread_raw_ = 0;
  int64_t acc_vwap_qty_raw_ = 0;
  int64_t acc_vwap_raw_ = 0;
  int64_t vwap_raw_ = 0;
  std::vector<int64_t> vwap_qty_raw_;
  std::vector<int64_t> vwap_price_raw_;
  struct BookTickerRaw {
    int64_t bid_price = 0;
    int64_t bid_qty = 0;
    int64_t ask_price = 0;
    int64_t ask_qty = 0;
  } book_ticker_raw_;
};
}  // namespace trading

#endif  // FEATURE_ENGINE_HPP
