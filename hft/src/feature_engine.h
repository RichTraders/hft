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
#include "types.h"

namespace trading {
class MarketOrderBook;
}

struct MarketData;

namespace common {
class Logger;
}

class FeatureEngine {
 public:
  explicit FeatureEngine(common::Logger* logger) : logger_(logger) {}

  auto on_trade_updated(const MarketData* market_update,
                        trading::MarketOrderBook* book) noexcept -> void;
  auto on_order_book_updated(common::Price price, common::Side side,
                             trading::MarketOrderBook* book) noexcept -> void;

  [[nodiscard]] auto get_mid_price() const noexcept { return mkt_price_; }

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
};

#endif  //FEATURE_ENGIN_H