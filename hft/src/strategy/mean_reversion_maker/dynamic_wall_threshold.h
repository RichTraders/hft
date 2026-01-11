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

#ifndef DYNAMIC_WALL_THRESHOLD_H
#define DYNAMIC_WALL_THRESHOLD_H

#include <algorithm>
#include <cstdint>
#include <vector>
#include "common/fixed_point_config.hpp"

namespace trading {

// === Configuration structures (int64_t version) ===
struct VolumeThresholdConfig {
  int64_t ema_alpha{300};    // 0.03 * kEmaScale
  int64_t multiplier{40000}; // 4.0 * kSignalScale
  int min_samples{20};
};

struct OrderbookThresholdConfig {
  int top_levels{20};
  int64_t multiplier{30000};  // 3.0 * kSignalScale
  int64_t percentile{8000};   // 80% * 100 (scaled to avoid division)
};

struct HybridThresholdConfig {
  int64_t volume_weight{7000};    // 0.7 * kSignalScale
  int64_t orderbook_weight{3000}; // 0.3 * kSignalScale
  int64_t min_quantity_raw{50000};  // min_quantity in qty scale (e.g., 50 * kQtyScale for 50 BTC)
};

// === Dynamic wall threshold calculator (int64_t version) ===
class DynamicWallThreshold {
 public:
  DynamicWallThreshold(const VolumeThresholdConfig& vol_cfg,
      const OrderbookThresholdConfig& ob_cfg,
      const HybridThresholdConfig& hybrid_cfg)
      : volume_ema_alpha_(vol_cfg.ema_alpha),
        volume_multiplier_(vol_cfg.multiplier),
        volume_min_samples_(vol_cfg.min_samples),
        ema_notional_raw_(0),
        sample_count_(0),
        volume_threshold_raw_(0),
        orderbook_top_levels_(ob_cfg.top_levels),
        orderbook_multiplier_(ob_cfg.multiplier),
        orderbook_percentile_(ob_cfg.percentile),
        orderbook_threshold_raw_(0),
        volume_weight_(hybrid_cfg.volume_weight),
        orderbook_weight_(hybrid_cfg.orderbook_weight),
        min_quantity_raw_(hybrid_cfg.min_quantity_raw),
        bid_qty_(ob_cfg.top_levels),
        ask_qty_(ob_cfg.top_levels),
        bid_quantities_(ob_cfg.top_levels),
        ask_quantities_(ob_cfg.top_levels) {
  }

  // === Main calculation function ===
  // Returns threshold in notional raw scale (price * qty / kQtyScale)
  template <typename MarketOrderBookT>
  [[nodiscard]] int64_t calculate(const MarketOrderBookT* order_book, uint64_t) {
    const auto* bbo = order_book->get_bbo();
    if (!bbo) return min_quantity_raw_;

    // mid_price in price_raw scale
    int64_t mid_price = (bbo->bid_price.value + bbo->ask_price.value) / 2;

    // min_threshold = min_quantity_raw * mid_price / kQtyScale
    // This gives notional in price_raw units
    int64_t min_threshold = (min_quantity_raw_ * mid_price) /
                            common::FixedPointConfig::kQtyScale;

    // Hybrid: weighted average
    // (volume * weight + orderbook * weight) / kSignalScale
    int64_t hybrid = (volume_threshold_raw_ * volume_weight_ +
                      orderbook_threshold_raw_ * orderbook_weight_) /
                     common::kSignalScale;

    return std::max(hybrid, min_threshold);
  }

  // === Feed trade data (realtime) - EMA update ===
  void on_trade(uint64_t, int64_t price_raw, int64_t qty_raw) {
    // notional = price * qty / kQtyScale (to get notional in price units)
    int64_t notional = (price_raw * qty_raw) / common::FixedPointConfig::kQtyScale;

    // EMA update: ema = alpha * new + (1-alpha) * old
    // = (alpha * new + (kEmaScale - alpha) * old) / kEmaScale
    if (sample_count_ == 0) {
      ema_notional_raw_ = notional;
    } else {
      ema_notional_raw_ = (volume_ema_alpha_ * notional +
                           (common::kEmaScale - volume_ema_alpha_) * ema_notional_raw_) /
                          common::kEmaScale;
    }

    sample_count_++;

    // Update threshold if enough samples
    // threshold = ema * multiplier / kSignalScale
    if (sample_count_ >= volume_min_samples_) {
      volume_threshold_raw_ = (ema_notional_raw_ * volume_multiplier_) /
                              common::kSignalScale;
    }
  }

  // === Update orderbook-based threshold (100ms interval) ===
  template <typename MarketOrderBookT>
  void update_orderbook_threshold(const MarketOrderBookT* order_book) {
    const auto* bbo = order_book->get_bbo();
    if (!bbo) {
      orderbook_threshold_raw_ = 0;
      return;
    }

    // Get quantities for top N levels
    (void)order_book->peek_qty(true, orderbook_top_levels_,
        std::span<int64_t>(bid_qty_), {});
    (void)order_book->peek_qty(false, orderbook_top_levels_,
        std::span<int64_t>(ask_qty_), {});

    // Copy to percentile buffers
    size_t bid_count = 0;
    size_t ask_count = 0;

    for (int i = 0; i < orderbook_top_levels_; ++i) {
      if (bid_qty_[i] > 0) {
        bid_quantities_[bid_count++] = bid_qty_[i];
      }
      if (ask_qty_[i] > 0) {
        ask_quantities_[ask_count++] = ask_qty_[i];
      }
    }

    if (bid_count == 0 || ask_count == 0) {
      orderbook_threshold_raw_ = 0;
      return;
    }

    // Calculate percentile using nth_element
    int64_t bid_percentile_qty = calculate_percentile_fast_bid(bid_count);
    int64_t ask_percentile_qty = calculate_percentile_fast_ask(ask_count);
    int64_t avg_qty = (bid_percentile_qty + ask_percentile_qty) / 2;

    // Convert to notional: qty * mid_price * multiplier / kSignalScale
    int64_t mid_price = (bbo->bid_price.value + bbo->ask_price.value) / 2;
    // notional = avg_qty * mid_price / kQtyScale * multiplier / kSignalScale
    orderbook_threshold_raw_ = (avg_qty * mid_price / common::FixedPointConfig::kQtyScale *
                                orderbook_multiplier_) / common::kSignalScale;
  }

  // === Getters ===
  [[nodiscard]] int64_t get_volume_threshold() const { return volume_threshold_raw_; }
  [[nodiscard]] int64_t get_orderbook_threshold() const { return orderbook_threshold_raw_; }
  [[nodiscard]] int64_t get_min_quantity() const { return min_quantity_raw_; }

 private:
  // percentile index = count * percentile / 10000
  [[nodiscard]] int64_t calculate_percentile_fast_bid(size_t count) {
    if (count == 0) return 0;

    size_t index = (count * static_cast<size_t>(orderbook_percentile_)) / 10000;
    index = std::min(index, count - 1);

    std::nth_element(bid_quantities_.begin(),
        bid_quantities_.begin() + static_cast<ptrdiff_t>(index),
        bid_quantities_.begin() + static_cast<ptrdiff_t>(count));
    return bid_quantities_[index];
  }

  [[nodiscard]] int64_t calculate_percentile_fast_ask(size_t count) {
    if (count == 0) return 0;

    size_t index = (count * static_cast<size_t>(orderbook_percentile_)) / 10000;
    index = std::min(index, count - 1);

    std::nth_element(ask_quantities_.begin(),
        ask_quantities_.begin() + static_cast<ptrdiff_t>(index),
        ask_quantities_.begin() + static_cast<ptrdiff_t>(count));
    return ask_quantities_[index];
  }

  // Volume-based threshold (EMA)
  int64_t volume_ema_alpha_;
  int64_t volume_multiplier_;
  int volume_min_samples_;
  int64_t ema_notional_raw_;
  int sample_count_;
  int64_t volume_threshold_raw_;

  // Orderbook-based threshold
  int orderbook_top_levels_;
  int64_t orderbook_multiplier_;
  int64_t orderbook_percentile_;
  int64_t orderbook_threshold_raw_;

  // Hybrid weights
  int64_t volume_weight_;
  int64_t orderbook_weight_;

  // Minimum quantity (raw)
  int64_t min_quantity_raw_;

  // Pre-allocated vectors
  std::vector<int64_t> bid_qty_;
  std::vector<int64_t> ask_qty_;
  std::vector<int64_t> bid_quantities_;
  std::vector<int64_t> ask_quantities_;
};

}  // namespace trading

#endif  // DYNAMIC_WALL_THRESHOLD_H
