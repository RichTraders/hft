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

#include <algorithm>
#include <cstdint>
#include <deque>
#include <numeric>
#include <vector>

#include "common/logger.h"
#include "common/types.h"
#include "common/fixed_point_config.hpp"
#include "core/market_data.h"
#include "order_book.hpp"

namespace trading {
template <typename Strategy>
class FeatureEngine {
 public:
  struct TradeInfo {
    common::Side side;
    int64_t price_raw;   // Price in raw scale (e.g., price * kPriceScale)
    int64_t qty_raw;     // Quantity in raw scale (e.g., qty * kQtyScale)
    uint64_t timestamp;
  };

  // Wall detection result structure (int64_t version)
  struct WallInfo {
    int64_t accumulated_notional{0};  // price * qty in raw scale
    int64_t distance_bps{0};              // Distance in basis points (15 = 0.15%)
    int levels_checked{0};
    bool is_valid{false};
  };

  // Wall quality tracking structure (int64_t version)
  struct WallTracker {
    uint64_t first_seen{0};
    uint64_t last_update{0};
    int snapshot_count{0};
    std::deque<int64_t> size_snapshots;      // Notional in raw scale
    std::deque<int64_t> distance_snapshots;  // Distance in bps

    void update(uint64_t now, int64_t notional_raw, int64_t distance_bps) {
      if (first_seen == 0) {
        first_seen = now;
      }
      last_update = now;
      snapshot_count++;

      size_snapshots.push_back(notional_raw);
      distance_snapshots.push_back(distance_bps);

      if (size_snapshots.size() > 20) {
        size_snapshots.pop_front();
        distance_snapshots.pop_front();
      }
    }

    void reset() {
      first_seen = 0;
      last_update = 0;
      snapshot_count = 0;
      size_snapshots.clear();
      distance_snapshots.clear();
    }

    // Persistence score: How long has wall been present?
    // Returns [0, kSignalScale] where kSignalScale = 10000
    // 2+ seconds = 10000, 1 second = 5000, 0.5 seconds = 0
    [[nodiscard]] int64_t persistence_score() const {
      if (snapshot_count < 5) return 0;
      // duration in nanoseconds / 2e9 * kSignalScale
      // = (duration * kSignalScale) / 2e9
      int64_t duration_ns = static_cast<int64_t>(last_update - first_seen);
      int64_t score = (duration_ns * common::kSignalScale) / 2'000'000'000;
      return std::clamp(score, int64_t{0}, common::kSignalScale);
    }

    // Stability score: Based on variance (no sqrt)
    // Low variance = high stability
    // Returns [0, kSignalScale]
    [[nodiscard]] int64_t stability_score() const {
      if (size_snapshots.size() < 10) return 0;

      // Calculate average
      int64_t sum = std::accumulate(size_snapshots.begin(), size_snapshots.end(), int64_t{0});
      int64_t avg = sum / static_cast<int64_t>(size_snapshots.size());

      if (avg == 0) return 0;

      // Calculate variance (sum of squared deviations)
      int64_t variance_sum = 0;
      for (int64_t size : size_snapshots) {
        int64_t diff = size - avg;
        // Use __int128 to avoid overflow in squaring
        __int128_t sq = static_cast<__int128_t>(diff) * diff;
        variance_sum += static_cast<int64_t>(sq / avg);  // Normalize by avg to keep in range
      }
      int64_t normalized_variance = variance_sum / static_cast<int64_t>(size_snapshots.size());

      // CV^2 threshold: if cv < 0.5, cv^2 < 0.25
      // normalized_variance / avg < 0.25 means stable
      // score = kSignalScale * (1 - normalized_variance / (avg * 0.25))
      // = kSignalScale * (1 - 4 * normalized_variance / avg)
      int64_t threshold = avg / 4;  // 0.25 * avg
      if (threshold == 0) return common::kSignalScale;

      int64_t score = common::kSignalScale - (normalized_variance * common::kSignalScale) / threshold;
      return std::clamp(score, int64_t{0}, common::kSignalScale);
    }

    // Distance consistency score
    // Close to BBO = good, far = bad
    // Returns [0, kSignalScale]
    [[nodiscard]] int64_t distance_consistency_score() const {
      if (distance_snapshots.size() < 10) return 0;

      int64_t sum = std::accumulate(distance_snapshots.begin(),
                                    distance_snapshots.end(), int64_t{0});
      int64_t avg_bps = sum / static_cast<int64_t>(distance_snapshots.size());

      // Close to BBO = good (< 5 bps = 10000)
      // Far from BBO = bad (> 15 bps = 0)
      // Linear interpolation: score = kSignalScale * (15 - avg) / 10
      // In bps: 5 bps = good, 15 bps = bad
      if (avg_bps <= 5) return common::kSignalScale;
      if (avg_bps >= 15) return 0;

      return common::kSignalScale * (15 - avg_bps) / 10;
    }

    // Composite quality score (weighted average)
    // Returns [0, kSignalScale]
    [[nodiscard]] int64_t composite_quality() const {
      // Weights: stability 50%, persistence 35%, distance 15%
      return (stability_score() * 5000 +
              persistence_score() * 3500 +
              distance_consistency_score() * 1500) / common::kSignalScale;
    }
  };

  explicit FeatureEngine(const common::Logger::Producer& logger)
      : logger_(logger),
        tick_multiplier_(INI_CONFIG.get_int("orderbook", "tick_multiplier_int")),
        vwap_size_(INI_CONFIG.get_int("strategy", "vwap_size", kVwapSize)),
        vwap_qty_raw_(vwap_size_),
        vwap_price_raw_(vwap_size_),
        recent_trades_(kMaxTradeHistory)
  {
    static_assert(kMaxTradeHistory > 0 && ((kMaxTradeHistory & kMaxTradeHistory - 1)== 0));
    logger_.info("[Constructor] FeatureEngine Created");
  }

  ~FeatureEngine() { std::cout << "[Destructor] FeatureEngine Destroy\n"; }

  auto on_trade_updated(const MarketData* market_update,
      MarketOrderBook<Strategy>* book) noexcept -> void {
    const auto* bbo = book->get_bbo();
    if (LIKELY(bbo->bid_price.value > 0 && bbo->ask_price.value > 0)) {
      // Calculate ratio in scaled form (kSignalScale)
      int64_t denom = (market_update->side == common::Side::kBuy)
                          ? bbo->ask_qty.value
                          : bbo->bid_qty.value;
      if (denom > 0) {
        agg_trade_qty_ratio_ = (market_update->qty.value * common::kSignalScale) / denom;
      }
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
      vwap_raw_ = acc_vwap_raw_ / acc_vwap_qty_raw_;
    }
    vwap_index_++;

    recent_trades_[trade_history_index_] = {
        market_update->side,
        market_update->price.value,
        market_update->qty.value,
        0
    };

    trade_history_index_ = (trade_history_index_ + 1) % kMaxTradeHistory;
    if (trade_history_count_ < kMaxTradeHistory) {
      trade_history_count_++;
    }

    logger_.trace("[Updated] {} mkt-price:{} agg-trade-ratio:{}",
        market_update->toString(),
        mkt_price_raw_,
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
      const int64_t num = bbo->bid_price.value * bbo->ask_qty.value +
                          bbo->ask_price.value * bbo->bid_qty.value;
      const int64_t den = bbo->bid_qty.value + bbo->ask_qty.value;
      if (den > 0) {
        mkt_price_raw_ = num / den;
      }
      spread_raw_ = bbo->ask_price.value - bbo->bid_price.value;
    }

    logger_.trace("[Updated] price:{} side:{} mkt-price:{} agg-trade-ratio:{}",
        common::toString(price),
        common::toString(side),
        mkt_price_raw_,
        agg_trade_qty_ratio_);
  }

  // OBI range: [-kObiScale, +kObiScale] representing [-1.0, +1.0]
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
    return (diff * common::kObiScale) / total;
  }

  [[nodiscard]] int64_t get_market_price() const noexcept { return mkt_price_raw_; }
  [[nodiscard]] int64_t get_mid_price() const noexcept {
    return (book_ticker_raw_.bid_price + book_ticker_raw_.ask_price) / 2;
  }
  [[nodiscard]] int64_t get_spread() const noexcept { return spread_raw_; }
  [[nodiscard]] int64_t get_spread_fast() const noexcept {
    return book_ticker_raw_.ask_price - book_ticker_raw_.bid_price;
  }
  [[nodiscard]] int64_t get_vwap() const noexcept { return vwap_raw_; }

  [[nodiscard]] int64_t get_agg_trade_qty_ratio() const noexcept {
    return agg_trade_qty_ratio_;
  }

  [[nodiscard]] const auto* get_recent_trades() const noexcept {
    return recent_trades_.data();
  }

  [[nodiscard]] auto get_trade_history_size() const noexcept {
    return trade_history_count_;
  }

  [[nodiscard]] auto get_trade_history_capacity() const noexcept {
    return kMaxTradeHistory;
  }

  // Helper to get trade by offset from most recent
  [[nodiscard]] const TradeInfo& get_trade(size_t offset) const noexcept {
    return recent_trades_[(trade_history_count_ - 1 - offset) % kMaxTradeHistory];
  }

  // ========================================
  // Trend acceleration detection (int64_t version)
  // ========================================
  [[nodiscard]] bool is_trend_accelerating(common::Side direction,
      int lookback_ticks, int consecutive_threshold,
      int64_t volume_multiplier_scaled) const noexcept {
    // volume_multiplier_scaled: 1.5 = 15000 (kSignalScale)
    if (trade_history_count_ < static_cast<size_t>(lookback_ticks)) {
      return false;
    }

    // === 1. Direction consistency check ===
    int consecutive_count = 0;
    size_t lookback = std::min(trade_history_count_,
        static_cast<size_t>(lookback_ticks));

    for (size_t i = 0; i < lookback; ++i) {
      size_t idx = (trade_history_count_ - lookback + i);
      if (recent_trades_[idx].side == direction) {
        consecutive_count++;
      }
    }

    if (consecutive_count < consecutive_threshold) {
      return false;
    }

    // === 2. Volume acceleration check (int64_t, no division) ===
    if (trade_history_count_ >= static_cast<size_t>(lookback_ticks)) {
      // Recent 2 ticks sum
      int64_t vol_recent_sum = get_trade(0).qty_raw + get_trade(1).qty_raw;

      // Previous 3 ticks sum
      int64_t vol_old_sum = get_trade(2).qty_raw +
                            get_trade(3).qty_raw +
                            get_trade(4).qty_raw;

      // Compare: vol_recent/2 > vol_old/3 * multiplier
      // Equivalent: vol_recent * 3 * kSignalScale > vol_old * 2 * multiplier_scaled
      if (vol_recent_sum * 3 * common::kSignalScale >
          vol_old_sum * 2 * volume_multiplier_scaled) {
        return true;
      }
    }

    return false;
  }

  // ========================================
  // Wall detection (int64_t version)
  // ========================================
  template <typename OrderBook>
  [[nodiscard]] WallInfo detect_wall(const OrderBook* order_book,
      common::Side side, int max_levels, int64_t threshold_notional_raw,
      int64_t max_distance_bps, int min_price_int,
      std::vector<int64_t>& level_qty_buffer,
      std::vector<int>& level_idx_buffer) const noexcept {
    WallInfo info;
    const auto* bbo = order_book->get_bbo();

    if (UNLIKELY(!bbo || bbo->bid_price == common::kPriceInvalid ||
                 bbo->ask_price == common::kPriceInvalid)) {
      return info;
    }

    const int64_t base_price = (side == common::Side::kBuy)
                                   ? bbo->bid_price.value
                                   : bbo->ask_price.value;

    if (base_price == 0) {
      return info;
    }

    // Peek orderbook levels
    int actual_levels = order_book->peek_qty(side == common::Side::kBuy,
        max_levels, std::span<int64_t>(level_qty_buffer),
        std::span<int>(level_idx_buffer));

    // For weighted average price calculation
    // Using __int128 to avoid overflow
    __int128_t weighted_sum = 0;

    for (int i = 0; i < actual_levels; ++i) {
      if (level_qty_buffer[i] <= 0)
        break;

      const int64_t price_idx = level_idx_buffer[i];
      const int64_t price_raw = min_price_int + price_idx;

      // notional = price * qty / kQtyScale (to normalize)
      // But we keep in raw scale for comparison
      const int64_t notional = (price_raw * level_qty_buffer[i]) /
                               common::FixedPointConfig::kQtyScale;
      info.accumulated_notional += notional;
      weighted_sum += static_cast<__int128_t>(price_raw) * notional;
      info.levels_checked = i + 1;

      // Target amount reached
      if (info.accumulated_notional >= threshold_notional_raw) {
        // weighted_avg_price = weighted_sum / accumulated
        int64_t weighted_avg_price = static_cast<int64_t>(
            weighted_sum / info.accumulated_notional);

        // distance_bps = |avg_price - base_price| * 10000 / base_price
        int64_t delta = std::abs(weighted_avg_price - base_price);
        info.distance_bps = (delta * common::kBpsScale) / base_price;
        info.is_valid = (info.distance_bps <= max_distance_bps);

        break;
      }
    }

    if (info.accumulated_notional < threshold_notional_raw) {
      info.is_valid = false;
    }

    return info;
  }

  FeatureEngine() = delete;

  FeatureEngine(const FeatureEngine&) = delete;

  FeatureEngine(const FeatureEngine&&) = delete;

  FeatureEngine& operator=(const FeatureEngine&) = delete;

  FeatureEngine& operator=(const FeatureEngine&&) = delete;

 private:
  static constexpr int kVwapSize = 64;
  static constexpr size_t kMaxTradeHistory = 128;
  const common::Logger::Producer& logger_;
  const int tick_multiplier_;
  int64_t agg_trade_qty_ratio_ = 0;  // Scaled by kSignalScale
  const uint32_t vwap_size_ = 0;
  uint32_t vwap_index_ = 0;

  int64_t mkt_price_raw_ = 0;
  int64_t spread_raw_ = 0;
  int64_t acc_vwap_qty_raw_ = 0;
  int64_t acc_vwap_raw_ = 0;
  int64_t vwap_raw_ = 0;
  std::vector<int64_t> vwap_qty_raw_;
  std::vector<int64_t> vwap_price_raw_;

  // Trade history tracking
  std::vector<TradeInfo> recent_trades_;
  size_t trade_history_index_{0};
  size_t trade_history_count_{0};
  struct BookTickerRaw {
    int64_t bid_price = 0;
    int64_t bid_qty = 0;
    int64_t ask_price = 0;
    int64_t ask_qty = 0;
  } book_ticker_raw_;
};
}  // namespace trading

#endif  // FEATURE_ENGINE_HPP