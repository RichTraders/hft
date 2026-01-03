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
#include "common/fixed_point_config.hpp"
#include "core/market_data.h"
#include "order_book.hpp"

namespace trading {
template <typename Strategy>
class FeatureEngine {
 public:
  // Trade history structure
  struct TradeInfo {
    common::Side side;
    double qty;
    double price;
    uint64_t timestamp;
  };

  // Wall detection result structure
  struct WallInfo {
    double accumulated_amount{0.0};
    double distance_pct{0.0};
    int levels_checked{0};
    bool is_valid{false};
  };

  // Wall quality tracking structure (spoofing detection)
  struct WallTracker {
    uint64_t first_seen{0};           // When wall was first detected
    uint64_t last_update{0};          // Last update timestamp
    int snapshot_count{0};            // Number of snapshots taken
    std::deque<double> size_snapshots;      // Last 20 size snapshots (100ms × 20 = 2sec)
    std::deque<double> distance_snapshots;  // Last 20 distance snapshots

    void update(uint64_t now, double size, double distance_pct) {
      if (first_seen == 0) {
        first_seen = now;
      }
      last_update = now;
      snapshot_count++;

      size_snapshots.push_back(size);
      distance_snapshots.push_back(distance_pct);

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
    // 2+ seconds = 1.0, 1 second = 0.5, 0.5 seconds = 0.0
    double persistence_score() const {
      if (snapshot_count < 5) return 0.0;  // Too new
      double duration_sec = (last_update - first_seen) / 1e9;
      return std::clamp(duration_sec / 2.0, 0.0, 1.0);
    }

    // Stability score: Coefficient of Variation (CV)
    // CV < 0.15 = 1.0 (very stable)
    // CV = 0.30 = 0.5
    // CV > 0.50 = 0.0 (spoofing)
    double stability_score() const {
      if (size_snapshots.size() < 10) return 0.0;

      // Calculate average
      double avg = std::accumulate(size_snapshots.begin(), size_snapshots.end(), 0.0)
                   / size_snapshots.size();

      if (avg < 1e-8) return 0.0;

      // Calculate variance
      double variance = 0.0;
      for (double size : size_snapshots) {
        variance += (size - avg) * (size - avg);
      }
      variance /= size_snapshots.size();

      // Coefficient of Variation
      double cv = std::sqrt(variance) / avg;

      // CV < 0.15 = 1.0 (very stable)
      // CV = 0.30 = 0.5
      // CV > 0.50 = 0.0 (spoofing)
      return std::clamp(1.0 - (cv / 0.5), 0.0, 1.0);
    }

    // Distance consistency score
    // Close to BBO = good (< 0.05% = 1.0)
    // Far from BBO = bad (> 0.15% = 0.0)
    double distance_consistency_score() const {
      if (distance_snapshots.size() < 10) return 0.0;

      double avg_dist = std::accumulate(distance_snapshots.begin(),
                                        distance_snapshots.end(), 0.0)
                        / distance_snapshots.size();

      // Close to BBO = good (< 0.05% = 1.0)
      // Far from BBO = bad (> 0.15% = 0.0)
      return std::clamp(1.0 - (avg_dist - 0.0005) / 0.001, 0.0, 1.0);
    }

    // Composite quality score (weighted average)
    double composite_quality() const {
      return 0.50 * stability_score() +
             0.35 * persistence_score() +
             0.15 * distance_consistency_score();
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
    logger_.info("[Constructor] FeatureEngine Created");
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

    // Store trade history (circular buffer - no allocation)
    recent_trades_[trade_history_index_] = {
        market_update->side,
        market_update->qty.value,
        market_update->price.value,
        0  // timestamp filled by strategy if needed
    };

    trade_history_index_ = (trade_history_index_ + 1) % kMaxTradeHistory;
    if (trade_history_count_ < kMaxTradeHistory) {
      trade_history_count_++;
    }

    logger_.trace("[Updated] {} mkt-price:{} agg-trade-ratio:{}",
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

    logger_.trace("[Updated] price:{} side:{} mkt-price:{} agg-trade-ratio:{}",
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
    if (den <= 0) return common::kPriceInvalid;
    // Result is in price_scale (price*qty/qty = price)
    // NOLINTNEXTLINE(bugprone-integer-division) - intentional integer division for VWAP
    return static_cast<double>(num / den) / common::FixedPointConfig::kPriceScale;
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

  [[nodiscard]] int64_t get_market_price() const noexcept { return mkt_price_raw_; }
  [[nodiscard]] int64_t get_mid_price() const noexcept {
    return (book_ticker_raw_.bid_price + book_ticker_raw_.ask_price) / 2;
  }
  [[nodiscard]] int64_t get_spread() const noexcept { return spread_raw_; }
  [[nodiscard]] int64_t get_spread_fast() const noexcept {
    return book_ticker_raw_.ask_price - book_ticker_raw_.bid_price;
  }
  [[nodiscard]] int64_t get_vwap() const noexcept { return vwap_raw_; }

  [[nodiscard]] double get_market_price_double() const noexcept {
    return static_cast<double>(mkt_price_raw_) / common::FixedPointConfig::kPriceScale;
  }

  [[nodiscard]] auto get_agg_trade_qty_ratio() const noexcept {
    return agg_trade_qty_ratio_;
  }

  [[nodiscard]] auto get_ofi() const noexcept { return ofi_; }

  [[nodiscard]] const auto* get_recent_trades() const noexcept {
    return recent_trades_.data();
  }

  [[nodiscard]] auto get_trade_history_size() const noexcept {
    return trade_history_count_;
  }

  [[nodiscard]] auto get_trade_history_capacity() const noexcept {
    return kMaxTradeHistory;
  }

  // ========================================
  // Trend acceleration detection (parameterized for all strategies)
  // ========================================
  [[nodiscard]] bool is_trend_accelerating(common::Side direction,
      int lookback_ticks, int consecutive_threshold,
      double volume_multiplier) const noexcept {
    if (trade_history_count_ < static_cast<size_t>(lookback_ticks)) {
      return false;
    }

    // === 1. Direction consistency check ===
    int consecutive_count = 0;
    size_t lookback = std::min(trade_history_count_,
        static_cast<size_t>(lookback_ticks));

    // Check most recent N trades (circular buffer)
    for (size_t i = 0; i < lookback; ++i) {
      size_t idx = (trade_history_count_ - lookback + i);
      if (recent_trades_[idx].side == direction) {
        consecutive_count++;
      }
    }

    if (consecutive_count < consecutive_threshold) {
      return false;  // Direction not strong enough
    }

    // === 2. Volume acceleration check (required) ===
    if (trade_history_count_ >= static_cast<size_t>(lookback_ticks)) {
      // Get recent trades (newest = highest index)
      auto get_trade = [&](size_t offset) -> const auto& {
        return recent_trades_[trade_history_count_ - 1 - offset];
      };

      // Recent 2 ticks average volume (most recent)
      double vol_recent = (get_trade(0).qty + get_trade(1).qty) / 2.0;

      // Previous 3 ticks average volume (older)
      double vol_old =
          (get_trade(2).qty + get_trade(3).qty + get_trade(4).qty) / 3.0;

      // Volume accelerating (strong trend signal)
      if (vol_recent > vol_old * volume_multiplier) {
        return true;  // Strong trend acceleration - BLOCK entry
      }
    }

    // Direction consistent but volume NOT accelerating
    // Allow entry - likely normal market movement, not dangerous trend
    return false;
  }

  // ========================================
  // Wall detection (parameterized for all strategies)
  // ========================================
  template <typename OrderBook>
  [[nodiscard]] WallInfo detect_wall(const OrderBook* order_book,
      common::Side side, int max_levels, double threshold_amount,
      double max_distance_pct, int min_price_int,
      std::vector<double>& level_qty_buffer,
      std::vector<int>& level_idx_buffer) const noexcept {
    WallInfo info;
    const auto* bbo = order_book->get_bbo();

    if (UNLIKELY(!bbo || bbo->bid_price == common::kPriceInvalid ||
                 bbo->ask_price == common::kPriceInvalid)) {
      return info;  // Invalid BBO
    }

    const double base_price = (side == common::Side::kBuy)
                                  ? bbo->bid_price.value
                                  : bbo->ask_price.value;

    // Peek orderbook levels
    int actual_levels = order_book->peek_qty(side == common::Side::kBuy,
        max_levels, level_qty_buffer, level_idx_buffer);

    double weighted_sum = 0.0;

    for (int i = 0; i < actual_levels; ++i) {
      if (level_qty_buffer[i] <= 0)
        break;

      // Calculate actual price from level index
      // IMPORTANT: price_idx is a RELATIVE index from min_price_int
      // Correct formula: (min_price_int + price_idx) / tick_multiplier
      const int64_t price_idx = level_idx_buffer[i];
      const double price = static_cast<double>(min_price_int + price_idx) / tick_multiplier_;


      const double notional = price * level_qty_buffer[i];
      info.accumulated_amount += notional;
      weighted_sum += price * notional;
      info.levels_checked = i + 1;

      // Target amount reached
      if (info.accumulated_amount >= threshold_amount) {
        double weighted_avg_price = weighted_sum / info.accumulated_amount;
        info.distance_pct =
            std::abs(weighted_avg_price - base_price) / base_price;
        info.is_valid = (info.distance_pct <= max_distance_pct);

        break;
      }
    }

    // Target amount not reached (vacuum)
    if (info.accumulated_amount < threshold_amount) {
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
  static constexpr size_t kMaxTradeHistory = 100;
  const int tick_multiplier_;
  const common::Logger::Producer& logger_;
  double agg_trade_qty_ratio_ = common::kQtyInvalid;
  const uint32_t vwap_size_ = 0;
  uint32_t vwap_index_ = 0;

  // OFI (Order Flow Imbalance) tracking
  int64_t ofi_raw_ = 0.0;
  int64_t prev_bid_qty_raw_ = 0.0;
  int64_t prev_ask_qty_raw_ = 0.0;

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
