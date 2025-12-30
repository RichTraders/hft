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

// === Dynamic wall threshold calculator ===
class DynamicWallThreshold {
 public:
  DynamicWallThreshold(double volume_ema_alpha, double volume_multiplier,
      int volume_min_samples, int orderbook_top_levels,
      double orderbook_multiplier, double orderbook_percentile,
      double volume_weight, double orderbook_weight, double min_quantity)
      : volume_ema_alpha_(volume_ema_alpha),
        volume_multiplier_(volume_multiplier),
        volume_min_samples_(volume_min_samples),
        ema_notional_(0.0),
        sample_count_(0),
        volume_threshold_(0.0),
        orderbook_top_levels_(orderbook_top_levels),
        orderbook_multiplier_(orderbook_multiplier),
        orderbook_percentile_(orderbook_percentile),
        orderbook_threshold_(0.0),
        volume_weight_(volume_weight),
        orderbook_weight_(orderbook_weight),
        min_quantity_(min_quantity),
        // Pre-allocate vectors for orderbook threshold calculation
        bid_qty_(orderbook_top_levels),
        ask_qty_(orderbook_top_levels) {}

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

    // Find 80th percentile of BTC quantities (not USDT notional)
    std::vector<double> bid_quantities;
    std::vector<double> ask_quantities;
    bid_quantities.reserve(orderbook_top_levels_);
    ask_quantities.reserve(orderbook_top_levels_);

    for (int i = 0; i < orderbook_top_levels_; ++i) {
      if (bid_qty_[i] > 0) {
        bid_quantities.push_back(bid_qty_[i]);
      }
      if (ask_qty_[i] > 0) {
        ask_quantities.push_back(ask_qty_[i]);
      }
    }

    if (bid_quantities.empty() || ask_quantities.empty()) {
      orderbook_threshold_ = 0.0;
      return;
    }

    // Calculate 80th percentile of BTC quantities
    double bid_percentile_qty =
        calculate_percentile(bid_quantities, orderbook_percentile_);
    double ask_percentile_qty =
        calculate_percentile(ask_quantities, orderbook_percentile_);
    double avg_qty = (bid_percentile_qty + ask_percentile_qty) / 2.0;

    // Convert to USDT using mid price
    double mid_price = (bbo->bid_price.value + bbo->ask_price.value) * 0.5;
    orderbook_threshold_ = avg_qty * orderbook_multiplier_ * mid_price;
  }

  // === Getters ===
  double get_volume_threshold() const { return volume_threshold_; }
  double get_orderbook_threshold() const { return orderbook_threshold_; }

 private:
  // === Calculate percentile ===
  double calculate_percentile(std::vector<double>& data,
      double percentile) const {
    if (data.empty())
      return 0.0;

    std::sort(data.begin(), data.end());
    size_t index = static_cast<size_t>(data.size() * percentile / 100.0);
    index = std::min(index, data.size() - 1);
    return data[index];
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
  std::vector<double> bid_notionals_;
  std::vector<double> ask_notionals_;
};

}  // namespace trading

#endif  // DYNAMIC_WALL_THRESHOLD_H
