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

#ifndef FEATURE_ENGIN_H
#define FEATURE_ENGIN_H
#include "order_book.h"
#include "types.h"

namespace trading {
class MarketOrderBook;
}

struct MarketData;

namespace common {
class Logger;
}
constexpr int kVwapSize = 64;
namespace trading {
class FeatureEngine {
 public:
  explicit FeatureEngine(common::Logger* logger) : logger_(logger) {
    logger_->info("FeatureEngine Created");
  }

  ~FeatureEngine() { logger_->info("FeatureEngine Destory"); }
  auto on_trade_updated(const MarketData* market_update,
                        MarketOrderBook* book) noexcept -> void;
  auto on_order_book_updated(common::Price price, common::Side side,
                             MarketOrderBook* book) noexcept -> void;
  static double vwap_from_levels(const std::vector<LevelView>& level);
  static double orderbook_imbalance_from_levels(
      const std::vector<double>& bid_levels,
      const std::vector<double>& ask_levels);

  [[nodiscard]] auto get_mid_price() const noexcept { return mkt_price_; }
  [[nodiscard]] auto get_spread() const noexcept { return spread_; }
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
  common::Logger* logger_ = nullptr;
  double mkt_price_ = common::kPriceInvalid;
  double agg_trade_qty_ratio_ = common::kQtyInvalid;
  double spread_ = common::kPriceInvalid;
  double acc_vwap_qty_ = 0.;
  double acc_vwap_ = 0.;
  double vwap_ = 0.;
  std::array<double, kVwapSize> vwap_qty_;
  std::array<double, kVwapSize> vwap_price_;
  int vwap_index_ = 0;
};
}  // namespace trading

#endif  //FEATURE_ENGIN_H