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
#include <cmath>
#include <deque>
#include <string>
#include <vector>

namespace trading {

// === Configuration structures ===
struct VolumeThresholdConfig {
  double ema_alpha{0.03};
  double multiplier{4.0};
  int min_samples{20};
};

struct OrderbookThresholdConfig {
  int top_levels{20};
  double multiplier{3.0};
  double percentile{80.0};
};

struct HybridThresholdConfig {
  double volume_weight{0.7};
  double orderbook_weight{0.3};
  double min_quantity{50.0};
};

// === Dynamic wall threshold calculator ===
class DynamicWallThreshold {
 public:
  DynamicWallThreshold(const VolumeThresholdConfig& vol_cfg,
      const OrderbookThresholdConfig& ob_cfg,
      const HybridThresholdConfig& hybrid_cfg)
      : volume_ema_alpha_(vol_cfg.ema_alpha),
        volume_multiplier_(vol_cfg.multiplier),
        volume_min_samples_(vol_cfg.min_samples),
        ema_notional_(0.0),
        sample_count_(0),
        volume_threshold_(0.0),
        orderbook_top_levels_(ob_cfg.top_levels),
        orderbook_multiplier_(ob_cfg.multiplier),
        orderbook_percentile_(ob_cfg.percentile),
        orderbook_threshold_(0.0),
        volume_weight_(hybrid_cfg.volume_weight),
        orderbook_weight_(hybrid_cfg.orderbook_weight),
        min_quantity_(hybrid_cfg.min_quantity),
        // Pre-allocate vectors for orderbook threshold calculation
        bid_qty_(ob_cfg.top_levels),
        ask_qty_(ob_cfg.top_levels),
        bid_quantities_(ob_cfg.top_levels),
        ask_quantities_(ob_cfg.top_levels) {
    // Vectors are pre-allocated with resize (not reserve)
    // No dynamic allocation during update_orderbook_threshold()
  }

  // === Main calculation function ===
  template <typename MarketOrderBookT>
  double calculate(const MarketOrderBookT* order_book, uint64_t) {
    // Get current mid price for quantity-based minimum
    const auto* bbo = order_book->get_bbo();
    double mid_price =
        bbo ? (bbo->bid_price.value + bbo->ask_price.value) * 0.5 : 0.0;
    double min_threshold_usdt = min_quantity_ * mid_price;

    // Hybrid: weighted average of volume and orderbook thresholds
    double hybrid = volume_threshold_ * volume_weight_ +
                    orderbook_threshold_ * orderbook_weight_;

    return std::max(hybrid, min_threshold_usdt);
  }

  // === Feed trade data (realtime) - EMA update ===
  void on_trade(uint64_t, double price, double qty) {
    double notional = price * qty;

    // EMA update: one-liner!
    if (sample_count_ == 0) {
      ema_notional_ = notional;  // Initialize with first sample
    } else {
      ema_notional_ = volume_ema_alpha_ * notional +
                      (1.0 - volume_ema_alpha_) * ema_notional_;
    }

    sample_count_++;

    // Update threshold if enough samples
    if (sample_count_ >= volume_min_samples_) {
      volume_threshold_ = ema_notional_ * volume_multiplier_;
    }
  }

  // === Update orderbook-based threshold (100ms interval) ===
  template <typename MarketOrderBookT>
  void update_orderbook_threshold(const MarketOrderBookT* order_book) {
    const auto* bbo = order_book->get_bbo();
    if (!bbo) {
      orderbook_threshold_ = 0.0;
      return;
    }

    // Get quantities for top N levels (BTC)
    (void)order_book->peek_qty(true, orderbook_top_levels_, bid_qty_, {});
    (void)order_book->peek_qty(false, orderbook_top_levels_, ask_qty_, {});

    // Copy to percentile buffers (no allocation - direct index)
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
      orderbook_threshold_ = 0.0;
      return;
    }

    // Calculate 80th percentile using nth_element (faster than sort)
    double bid_percentile_qty =
        calculate_percentile_fast(bid_count, orderbook_percentile_);
    double ask_percentile_qty =
        calculate_percentile_fast_ask(ask_count, orderbook_percentile_);
    double avg_qty = (bid_percentile_qty + ask_percentile_qty) / 2.0;

    // Convert to USDT using mid price
    double mid_price = (bbo->bid_price.value + bbo->ask_price.value) * 0.5;
    orderbook_threshold_ = avg_qty * orderbook_multiplier_ * mid_price;
  }

  // === Getters ===
  double get_volume_threshold() const { return volume_threshold_; }
  double get_orderbook_threshold() const { return orderbook_threshold_; }
  double get_min_quantity() const { return min_quantity_; }

 private:
  // === Calculate percentile (fast - uses nth_element, not full sort) ===
  double calculate_percentile_fast(size_t count, double percentile) {
    if (count == 0)
      return 0.0;

    size_t index = static_cast<size_t>(count * percentile / 100.0);
    index = std::min(index, count - 1);

    // O(n) instead of O(n log n) - only partial sort
    std::nth_element(bid_quantities_.begin(),
        bid_quantities_.begin() + index,
        bid_quantities_.begin() + count);
    return bid_quantities_[index];
  }

  double calculate_percentile_fast_ask(size_t count, double percentile) {
    if (count == 0)
      return 0.0;

    size_t index = static_cast<size_t>(count * percentile / 100.0);
    index = std::min(index, count - 1);

    // O(n) instead of O(n log n) - only partial sort
    std::nth_element(ask_quantities_.begin(),
        ask_quantities_.begin() + index,
        ask_quantities_.begin() + count);
    return ask_quantities_[index];
  }

  // === Member variables ===
  // Volume-based threshold (EMA)
  double volume_ema_alpha_;
  double volume_multiplier_;
  int volume_min_samples_;
  double ema_notional_;
  int sample_count_;
  double volume_threshold_;

  // Orderbook-based threshold
  int orderbook_top_levels_;
  double orderbook_multiplier_;
  double orderbook_percentile_;
  double orderbook_threshold_;

  // Hybrid weights
  double volume_weight_;
  double orderbook_weight_;

  // Minimum quantity (BTC) - auto-scales with price
  double min_quantity_;

  // Pre-allocated vectors for orderbook threshold calculation (avoid repeated heap allocation)
  std::vector<double> bid_qty_;
  std::vector<double> ask_qty_;
  std::vector<double> bid_quantities_;
  std::vector<double> ask_quantities_;
};

}  // namespace trading

#endif  // DYNAMIC_WALL_THRESHOLD_H
