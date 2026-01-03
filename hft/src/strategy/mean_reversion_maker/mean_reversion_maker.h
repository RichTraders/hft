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

#ifndef MEAN_REVERSION_MAKER_H
#define MEAN_REVERSION_MAKER_H

#include <chrono>
#include <deque>
#include <memory>
#include <numeric>
#include <string>
#include <vector>
#include "../base_strategy.hpp"
#include "common/ini_config.hpp"
#include "dynamic_wall_threshold.h"
#include "feature_engine.hpp"
#include "oe_traits_config.hpp"
#include "order_book.hpp"
#include "order_manager.hpp"
#include "robust_zscore.h"

struct MarketData;

namespace trading {

// === Strategy Configuration Structures (int64_t version) ===
struct WallDetectionConfig {
  int64_t max_distance_bps{
      15};  // 0.15% = 15 bps (scaled by kBpsScale=10000 for calcs)
  int max_levels{100};
};

struct EntryConfig {
  int64_t obi_threshold{2500};  // 0.25 * kObiScale (10000)
  int obi_levels{5};
  int64_t position_size_raw{10};  // 0.01 * kQtyScale (1000)
  int64_t safety_margin_bps{5};   // 0.00005 = 0.5 bps
  int64_t min_spread_bps{40};     // 0.0004 = 4 bps

  // Multi-Factor Scoring parameters (all scaled by kSignalScale=10000)
  int64_t min_signal_quality{6500};  // 0.65 * kSignalScale
  int64_t zscore_weight{3500};       // 0.35 * kSignalScale
  int64_t wall_weight{3000};         // 0.30 * kSignalScale
  int64_t volume_weight{2000};       // 0.20 * kSignalScale
  int64_t obi_weight{1500};          // 0.15 * kSignalScale

  // Z-score normalization (scaled by kZScoreScale=10000)
  int64_t zscore_norm_min{20000};  // 2.0 * kZScoreScale
  int64_t zscore_norm_max{30000};  // 3.0 * kZScoreScale

  // Wall normalization (scaled by kSignalScale)
  int64_t wall_norm_multiplier{20000};  // 2.0 * kSignalScale

  // OBI normalization (scaled by kObiScale=10000)
  int64_t obi_norm_min{500};   // 0.05 * kObiScale
  int64_t obi_norm_max{2500};  // 0.25 * kObiScale

  int volume_score_lookback{5};  // Volume analysis window
};

struct ExitConfig {
  bool enabled{true};  // Enable/disable position exit monitoring
  int64_t wall_amount_decay_ratio{5000};      // 0.5 * kSignalScale
  int64_t wall_distance_expand_ratio{12000};  // 1.2 * kSignalScale
  int64_t max_loss_bps{20};                   // 0.2% = 20 bps
  uint64_t max_hold_time_ns{5'000'000'000};   // 5 seconds default (HFT)
  int64_t max_price_deviation_bps{20};        // 0.2% = 20 bps
  bool cancel_on_wall_decay{true};

  // Active exit conditions (profit-taking)
  int64_t zscore_exit_threshold{5000};   // 0.5 * kZScoreScale
  int64_t obi_exit_threshold{3000};      // 0.3 * kObiScale
  bool reversal_momentum_exit{true};     // Enable volume reversal exit
  int exit_lookback_ticks{10};           // Exit momentum lookback
  int exit_min_directional_ticks{7};     // 70% directional ticks required
  int64_t exit_min_volume_ratio{15000};  // 1.5 * kSignalScale
};

struct ReversalMomentumConfig {
  bool enabled{true};
  int lookback_ticks{5};
  int min_directional_ticks{3};
  int64_t min_volume_ratio{12000};  // 1.2 * kSignalScale
};

struct DebugLoggingConfig {
  bool log_wall_detection{false};
  bool log_defense_check{false};
  bool log_entry_exit{false};
};

struct AdverseSelectionConfig {
  int max_fill_history{20};
  uint64_t measurement_window_ns{1000000000};    // 1 second
  uint64_t measurement_tolerance_ns{100000000};  // ±100ms
  int64_t adverse_threshold_bps{2};              // 0.02% = 2 bps
  int min_samples{10};
  int64_t ratio_threshold{5000};     // 0.5 * kSignalScale
  int64_t margin_multiplier{15000};  // 1.5 * kSignalScale
};

struct MeanReversionConfig {
  // Legacy parameters (backwards compatibility) - scaled by kZScoreScale
  int64_t oversold_start_threshold{15000};    // 1.5 * kZScoreScale (Deprecated)
  int64_t overbought_start_threshold{15000};  // 1.5 * kZScoreScale (Deprecated)
  int64_t min_reversal_bounce{2000};          // 0.2 * kZScoreScale
  int64_t neutral_zone_threshold{10000};      // 1.0 * kZScoreScale

  // 5-State threshold multipliers (scaled by kSignalScale)
  int64_t building_multiplier{10000};        // 1.0 * kSignalScale
  int64_t deep_multiplier{12000};            // 1.2 * kSignalScale
  int64_t reversal_weak_multiplier{8000};    // 0.8 * kSignalScale
  int64_t reversal_strong_multiplier{6000};  // 0.6 * kSignalScale

  // False reversal detection (scaled by kSignalScale)
  int64_t false_reversal_ratio{5000};  // 0.5 * kSignalScale
};

// ==========================================
// Multi-Factor Signal Scoring (int64_t version)
// ==========================================
/**
 * @brief Entry signal quality score (scaled by kSignalScale=10000)
 *
 * Replaces boolean entry signals with scored signals to capture
 * signal strength and filter low-quality setups.
 *
 * Example:
 * - Z-score -2.1 → z_score_strength = 1000 (0.1 * kSignalScale)
 * - Z-score -3.0 → z_score_strength = 10000 (1.0 * kSignalScale)
 * - composite() = weighted average of all components
 */
struct SignalScore {
  int64_t z_score_strength{
      0};                    // [0, kSignalScale]: Z-score magnitude normalized
  int64_t wall_strength{0};  // [0, kSignalScale]: Wall size vs threshold
  int64_t volume_strength{0};  // [0, kSignalScale]: Directional volume momentum
  int64_t obi_strength{0};  // [0, kSignalScale]: Orderbook imbalance alignment

  /**
   * @brief Calculate composite score (weighted average)
   * @param cfg Entry config with component weights (all scaled by kSignalScale)
   * @return Composite score [0, kSignalScale]
   *
   * Formula: sum(weight_i * strength_i) / kSignalScale
   * Since weights sum to kSignalScale and strengths are [0, kSignalScale],
   * result is [0, kSignalScale]
   */
  [[nodiscard]] int64_t composite(const EntryConfig& cfg) const {
    return (cfg.zscore_weight * z_score_strength +
               cfg.wall_weight * wall_strength +
               cfg.volume_weight * volume_strength +
               cfg.obi_weight * obi_strength) /
           common::kSignalScale;
  }

  /**
   * @brief Get signal quality classification
   * @param cfg Entry config with min_signal_quality threshold
   * @return Quality level (EXCELLENT/GOOD/MARGINAL/POOR)
   */
  enum class Quality { EXCELLENT, GOOD, MARGINAL, POOR };

  [[nodiscard]] Quality get_quality(const EntryConfig& cfg) const {
    int64_t score = composite(cfg);
    if (score > 8000)  // 0.8 * kSignalScale
      return Quality::EXCELLENT;
    if (score >= cfg.min_signal_quality)
      return Quality::GOOD;
    if (score > 5000)  // 0.5 * kSignalScale
      return Quality::MARGINAL;
    return Quality::POOR;
  }
};

// ==========================================
// Adverse Selection Detection (Markout Analysis) - int64_t version
// ==========================================
/**
 * @brief Tracks fill-to-price movement to detect adverse selection
 *
 * Monitors whether strategy is being "picked off" by informed traders.
 * Pattern: Long filled → price drops immediately = adverse selection
 */
struct AdverseSelectionTracker {
  struct FillRecord {
    uint64_t fill_time{0};          // Fill timestamp (ns)
    int64_t fill_price_raw{0};      // Fill price in raw scale
    common::Side side;              // Buy or Sell
    int64_t price_1s_later_raw{0};  // Price 1 second after fill
    bool measured{false};           // Measurement complete flag
  };

  std::deque<FillRecord> recent_fills;
  int adverse_count{0};
  int total_measured{0};

  /**
   * @brief Record a new fill
   */
  void on_fill(uint64_t time, int64_t price_raw, common::Side side,
      int max_history) {
    recent_fills.push_back({time, price_raw, side, 0, false});
    if (static_cast<int>(recent_fills.size()) > max_history) {
      recent_fills.pop_front();
    }
  }

  /**
   * @brief Update fill records with current price (measure markout)
   * @param now Current timestamp (ns)
   * @param current_price_raw Current price in raw scale
   * @param cfg Config with thresholds
   */
  void on_price_update(uint64_t now, int64_t current_price_raw,
      const AdverseSelectionConfig& cfg) {
    for (auto& fill : recent_fills) {
      if (fill.measured)
        continue;

      uint64_t elapsed = now - fill.fill_time;

      // Measure 1 second (±100ms) after fill
      if (elapsed >= cfg.measurement_window_ns - cfg.measurement_tolerance_ns &&
          elapsed < cfg.measurement_window_ns + cfg.measurement_tolerance_ns) {
        fill.price_1s_later_raw = current_price_raw;
        fill.measured = true;

        // Check if adverse: return in bps = (current - fill) * 10000 / fill
        int64_t delta = current_price_raw - fill.fill_price_raw;
        int64_t ret_bps = (delta * common::kBpsScale) / fill.fill_price_raw;
        total_measured++;

        if (fill.side == common::Side::kBuy &&
            ret_bps < -cfg.adverse_threshold_bps) {
          adverse_count++;  // Bought then dropped = adverse
        } else if (fill.side == common::Side::kSell &&
                   ret_bps > cfg.adverse_threshold_bps) {
          adverse_count++;  // Sold then rose = adverse
        }
      }
    }
  }

  /**
   * @brief Get adverse selection ratio (scaled by kSignalScale)
   * @return Ratio of adverse fills [0, kSignalScale]
   */
  [[nodiscard]] int64_t get_ratio(int min_samples) const {
    if (total_measured < min_samples)
      return 0;
    return (static_cast<int64_t>(adverse_count) * common::kSignalScale) /
           total_measured;
  }

  /**
   * @brief Check if strategy is being picked off
   */
  [[nodiscard]] bool is_being_picked_off(
      const AdverseSelectionConfig& cfg) const {
    return get_ratio(cfg.min_samples) > cfg.ratio_threshold;
  }

  /**
   * @brief Reset counters (optional, for periodic recalibration)
   */
  void reset() {
    adverse_count = 0;
    total_measured = 0;
    recent_fills.clear();
  }
};

class MeanReversionMakerStrategy
    : public BaseStrategy<MeanReversionMakerStrategy> {
 public:
  using QuoteIntentType =
      std::conditional_t<SelectedOeTraits::supports_position_side(),
          FuturesQuoteIntent, SpotQuoteIntent>;
  using OrderManagerT = OrderManager<MeanReversionMakerStrategy>;
  using FeatureEngineT = FeatureEngine<MeanReversionMakerStrategy>;
  using MarketOrderBookT = MarketOrderBook<MeanReversionMakerStrategy>;

  // === Position state enumeration ===
  enum class PositionStatus : uint8_t {
    NONE = 0,     // No position, no pending order
    PENDING = 1,  // Order sent, waiting for fill
    ACTIVE = 2    // Position filled and active
  };

  // === Mean reversion phase enumeration (5-State for volatility adaptation) ===
  enum class ReversionPhase : uint8_t {
    NEUTRAL = 0,        // |z| < neutral_threshold (1.0)
    BUILDING_OVERSOLD,  // -adaptive_threshold < z < -neutral_threshold
    DEEP_OVERSOLD,      // z < -adaptive_threshold × deep_multiplier
    REVERSAL_WEAK,      // Bounced, but z still in weak reversal zone
    REVERSAL_STRONG     // Bounced strongly, ready for entry
  };

  // === Market regime enumeration (lightweight trend detection) ===
  enum class MarketRegime : uint8_t {
    RANGING = 0,    // Ranging market - mean reversion works
    TRENDING_UP,    // Uptrend - avoid shorts
    TRENDING_DOWN,  // Downtrend - avoid longs
    VOLATILE        // High volatility - reduce size
  };

  // === Position state structure (int64_t version) ===
  struct PositionState {
    int64_t qty{0};          // Quantity in raw scale (qty * kQtyScale)
    int64_t entry_price{0};  // Entry price in raw scale
    FeatureEngineT::WallInfo entry_wall_info;
    PositionStatus status{PositionStatus::NONE};
    uint64_t state_time{0};  // PENDING: order sent time, ACTIVE: fill time
    std::optional<common::OrderId> pending_order_id;  // Track expected order
    bool is_regime_override{
        false};  // Flag: entered against trend (risky, quick exit)
  };

  // Note: TradeRecord is now managed by FeatureEngine::TradeInfo

  MeanReversionMakerStrategy(OrderManagerT* order_manager,
      const FeatureEngineT* feature_engine,
      const InventoryManager* inventory_manager,
      PositionKeeper* position_keeper, const common::Logger::Producer& logger,
      const common::TradeEngineCfgHashMap&)
      : BaseStrategy(order_manager, feature_engine, inventory_manager,
            position_keeper, logger),
        // === Position mode ===
        allow_simultaneous_positions_(
            INI_CONFIG.get_int("strategy", "allow_simultaneous_positions", 0)),

        // === Defense (int64_t conversion: * kSignalScale) ===
        defense_qty_multiplier_(static_cast<int64_t>(
            INI_CONFIG.get_double("wall_defense", "qty_multiplier", 2.0) *
            common::kSignalScale)),

        // === Z-score threshold (int64_t conversion: * kZScoreScale) ===
        zscore_entry_threshold_(static_cast<int64_t>(
            INI_CONFIG.get_double("robust_zscore", "entry_threshold", 2.5) *
            common::kZScoreScale)),

        // === Config structures (all values converted to int64_t) ===
        wall_cfg_{static_cast<int64_t>(INI_CONFIG.get_double("wall_detection",
                                           "max_distance_pct", 0.0015) *
                                       common::kBpsScale),  // bps
            INI_CONFIG.get_int("wall_detection", "max_levels", 100)},

        entry_cfg_{static_cast<int64_t>(
                       INI_CONFIG.get_double("entry", "obi_threshold", 0.25) *
                       common::kObiScale),
            INI_CONFIG.get_int("entry", "obi_levels", 5),
            static_cast<int64_t>(
                INI_CONFIG.get_double("entry", "position_size", 0.01) *
                common::FixedPointConfig::kQtyScale),
            static_cast<int64_t>(
                INI_CONFIG.get_double("entry", "safety_margin", 0.00005) *
                common::kBpsScale),
            static_cast<int64_t>(
                INI_CONFIG.get_double("entry", "min_spread_filter", 0.0004) *
                common::kBpsScale),
            // Multi-Factor Scoring (all * kSignalScale)
            static_cast<int64_t>(
                INI_CONFIG.get_double("entry", "min_signal_quality", 0.65) *
                common::kSignalScale),
            static_cast<int64_t>(
                INI_CONFIG.get_double("entry", "zscore_weight", 0.35) *
                common::kSignalScale),
            static_cast<int64_t>(
                INI_CONFIG.get_double("entry", "wall_weight", 0.30) *
                common::kSignalScale),
            static_cast<int64_t>(
                INI_CONFIG.get_double("entry", "volume_weight", 0.20) *
                common::kSignalScale),
            static_cast<int64_t>(
                INI_CONFIG.get_double("entry", "obi_weight", 0.15) *
                common::kSignalScale),
            static_cast<int64_t>(
                INI_CONFIG.get_double("entry", "zscore_norm_min", 2.0) *
                common::kZScoreScale),
            static_cast<int64_t>(
                INI_CONFIG.get_double("entry", "zscore_norm_max", 3.0) *
                common::kZScoreScale),
            static_cast<int64_t>(
                INI_CONFIG.get_double("entry", "wall_norm_multiplier", 2.0) *
                common::kSignalScale),
            static_cast<int64_t>(
                INI_CONFIG.get_double("entry", "obi_norm_min", 0.05) *
                common::kObiScale),
            static_cast<int64_t>(
                INI_CONFIG.get_double("entry", "obi_norm_max", 0.25) *
                common::kObiScale),
            INI_CONFIG.get_int("entry", "volume_score_lookback", 5)},

        exit_cfg_{INI_CONFIG.get("exit", "enabled", "true") == "true",
            static_cast<int64_t>(
                INI_CONFIG.get_double("exit", "wall_amount_decay_ratio", 0.5) *
                common::kSignalScale),
            static_cast<int64_t>(INI_CONFIG.get_double("exit",
                                     "wall_distance_expand_ratio", 1.2) *
                                 common::kSignalScale),
            static_cast<int64_t>(
                INI_CONFIG.get_double("exit", "max_loss_pct", 0.002) *
                common::kBpsScale),
            static_cast<uint64_t>(
                INI_CONFIG.get_double("exit", "max_hold_time_sec", 5.0) *
                1'000'000'000),
            static_cast<int64_t>(INI_CONFIG.get_double("exit",
                                     "max_price_deviation_pct", 0.002) *
                                 common::kBpsScale),
            INI_CONFIG.get("exit", "cancel_on_wall_decay", "true") == "true",
            static_cast<int64_t>(
                INI_CONFIG.get_double("exit", "zscore_exit_threshold", 0.5) *
                common::kZScoreScale),
            static_cast<int64_t>(
                INI_CONFIG.get_double("exit", "obi_exit_threshold", 0.3) *
                common::kObiScale),
            INI_CONFIG.get("exit", "reversal_momentum_exit", "true") == "true",
            INI_CONFIG.get_int("exit", "exit_lookback_ticks", 10),
            INI_CONFIG.get_int("exit", "exit_min_directional_ticks", 7),
            static_cast<int64_t>(
                INI_CONFIG.get_double("exit", "exit_min_volume_ratio", 1.5) *
                common::kSignalScale)},

        reversal_cfg_{
            INI_CONFIG.get("reversal_momentum", "enabled", "true") == "true",
            INI_CONFIG.get_int("reversal_momentum", "lookback_ticks", 5),
            INI_CONFIG.get_int("reversal_momentum", "min_directional_ticks", 3),
            static_cast<int64_t>(INI_CONFIG.get_double("reversal_momentum",
                                     "min_volume_ratio", 1.2) *
                                 common::kSignalScale)},

        debug_cfg_{
            INI_CONFIG.get("debug", "log_wall_detection", "false") == "true",
            INI_CONFIG.get("debug", "log_defense_check", "false") == "true",
            INI_CONFIG.get("debug", "log_entry_exit", "false") == "true"},

        mean_reversion_cfg_{
            static_cast<int64_t>(INI_CONFIG.get_double("mean_reversion",
                                     "oversold_start_threshold", 1.5) *
                                 common::kZScoreScale),
            static_cast<int64_t>(INI_CONFIG.get_double("mean_reversion",
                                     "overbought_start_threshold", 1.5) *
                                 common::kZScoreScale),
            static_cast<int64_t>(INI_CONFIG.get_double("mean_reversion",
                                     "min_reversal_bounce", 0.2) *
                                 common::kZScoreScale),
            static_cast<int64_t>(INI_CONFIG.get_double("mean_reversion",
                                     "neutral_zone_threshold", 1.0) *
                                 common::kZScoreScale),
            static_cast<int64_t>(INI_CONFIG.get_double("mean_reversion",
                                     "building_multiplier", 1.0) *
                                 common::kSignalScale),
            static_cast<int64_t>(INI_CONFIG.get_double("mean_reversion",
                                     "deep_multiplier", 1.2) *
                                 common::kSignalScale),
            static_cast<int64_t>(INI_CONFIG.get_double("mean_reversion",
                                     "reversal_weak_multiplier", 0.8) *
                                 common::kSignalScale),
            static_cast<int64_t>(INI_CONFIG.get_double("mean_reversion",
                                     "reversal_strong_multiplier", 0.6) *
                                 common::kSignalScale),
            static_cast<int64_t>(INI_CONFIG.get_double("mean_reversion",
                                     "false_reversal_ratio", 0.5) *
                                 common::kSignalScale)},

        adverse_selection_cfg_{
            INI_CONFIG.get_int("adverse_selection", "max_fill_history", 20),
            static_cast<uint64_t>(INI_CONFIG.get_double("adverse_selection",
                "measurement_window_ns", 1'000'000'000)),
            static_cast<uint64_t>(INI_CONFIG.get_double("adverse_selection",
                "measurement_tolerance_ns", 100'000'000)),
            static_cast<int64_t>(INI_CONFIG.get_double("adverse_selection",
                                     "adverse_threshold_pct", 0.0002) *
                                 common::kBpsScale),
            INI_CONFIG.get_int("adverse_selection", "min_samples", 10),
            static_cast<int64_t>(INI_CONFIG.get_double("adverse_selection",
                                     "ratio_threshold", 0.5) *
                                 common::kSignalScale),
            static_cast<int64_t>(INI_CONFIG.get_double("adverse_selection",
                                     "margin_multiplier", 1.5) *
                                 common::kSignalScale)},

        // === Z-score config ===
        zscore_window_size_(
            INI_CONFIG.get_int("robust_zscore", "window_size", 30)),
        zscore_min_samples_(
            INI_CONFIG.get_int("robust_zscore", "min_samples", 20)),
        zscore_min_mad_threshold_raw_(static_cast<int64_t>(
            INI_CONFIG.get_double("robust_zscore", "min_mad_threshold", 5.0) *
            common::FixedPointConfig::kPriceScale)),

        // === Multi-timeframe Z-score config ===
        zscore_fast_window_(
            INI_CONFIG.get_int("robust_zscore_fast", "window_size", 10)),
        zscore_fast_min_samples_(
            INI_CONFIG.get_int("robust_zscore_fast", "min_samples", 8)),
        zscore_slow_window_(
            INI_CONFIG.get_int("robust_zscore_slow", "window_size", 100)),
        zscore_slow_min_samples_(
            INI_CONFIG.get_int("robust_zscore_slow", "min_samples", 60)),
        zscore_slow_threshold_(
            static_cast<int64_t>(INI_CONFIG.get_double("robust_zscore_slow",
                                     "entry_threshold", 1.5) *
                                 common::kZScoreScale)),

        // === OBI calculation buffers ===
        bid_qty_(entry_cfg_.obi_levels),
        ask_qty_(entry_cfg_.obi_levels),

        // === Wall detection buffers ===
        wall_level_qty_(wall_cfg_.max_levels),
        wall_level_idx_(wall_cfg_.max_levels),

        current_wall_threshold_raw_(0),

        // === Dynamic threshold module (int64_t config) ===
        dynamic_threshold_(std::make_unique<DynamicWallThreshold>(
            VolumeThresholdConfig{
                static_cast<int64_t>(INI_CONFIG.get_double("wall_defense",
                                         "volume_ema_alpha", 0.03) *
                                     common::kEmaScale),
                static_cast<int64_t>(INI_CONFIG.get_double("wall_defense",
                                         "volume_multiplier", 4.0) *
                                     common::kSignalScale),
                INI_CONFIG.get_int("wall_defense", "volume_min_samples", 20)},
            OrderbookThresholdConfig{
                INI_CONFIG.get_int("wall_defense", "orderbook_top_levels", 20),
                static_cast<int64_t>(INI_CONFIG.get_double("wall_defense",
                                         "orderbook_multiplier", 3.0) *
                                     common::kSignalScale),
                static_cast<int64_t>(INI_CONFIG.get_double("wall_defense",
                                         "orderbook_percentile", 80) *
                                     100)},  // 80% = 8000
            HybridThresholdConfig{
                static_cast<int64_t>(INI_CONFIG.get_double("wall_defense",
                                         "volume_weight", 0.7) *
                                     common::kSignalScale),
                static_cast<int64_t>(INI_CONFIG.get_double("wall_defense",
                                         "orderbook_weight", 0.3) *
                                     common::kSignalScale),
                static_cast<int64_t>(INI_CONFIG.get_double("wall_defense",
                                         "min_quantity", 50.0) *
                                     common::FixedPointConfig::kQtyScale)})),

        // === Robust Z-score modules (multi-timeframe) - int64_t config ===
        robust_zscore_fast_(std::make_unique<RobustZScore>(RobustZScoreConfig{
            zscore_fast_window_,
            zscore_fast_min_samples_,
            zscore_min_mad_threshold_raw_,
            INI_CONFIG.get_int("robust_zscore_fast", "baseline_window", 100),
            static_cast<int64_t>(INI_CONFIG.get_double("robust_zscore_fast",
                                     "min_vol_scalar", 0.7) *
                                 common::kSignalScale),
            static_cast<int64_t>(INI_CONFIG.get_double("robust_zscore_fast",
                                     "max_vol_scalar", 1.3) *
                                 common::kSignalScale),
            static_cast<int64_t>(INI_CONFIG.get_double("robust_zscore_fast",
                                     "vol_ratio_low", 0.5) *
                                 common::kSignalScale),
            static_cast<int64_t>(INI_CONFIG.get_double("robust_zscore_fast",
                                     "vol_ratio_high", 2.0) *
                                 common::kSignalScale),
            INI_CONFIG.get_int("robust_zscore_fast", "baseline_min_history",
                30)})),

        robust_zscore_mid_(std::make_unique<RobustZScore>(RobustZScoreConfig{
            zscore_window_size_,
            zscore_min_samples_,
            zscore_min_mad_threshold_raw_,
            INI_CONFIG.get_int("robust_zscore", "baseline_window", 100),
            static_cast<int64_t>(
                INI_CONFIG.get_double("robust_zscore", "min_vol_scalar", 0.7) *
                common::kSignalScale),
            static_cast<int64_t>(
                INI_CONFIG.get_double("robust_zscore", "max_vol_scalar", 1.3) *
                common::kSignalScale),
            static_cast<int64_t>(
                INI_CONFIG.get_double("robust_zscore", "vol_ratio_low", 0.5) *
                common::kSignalScale),
            static_cast<int64_t>(
                INI_CONFIG.get_double("robust_zscore", "vol_ratio_high", 2.0) *
                common::kSignalScale),
            INI_CONFIG.get_int("robust_zscore", "baseline_min_history", 30)})),

        robust_zscore_slow_(std::make_unique<RobustZScore>(RobustZScoreConfig{
            zscore_slow_window_,
            zscore_slow_min_samples_,
            zscore_min_mad_threshold_raw_,
            INI_CONFIG.get_int("robust_zscore_slow", "baseline_window", 100),
            static_cast<int64_t>(INI_CONFIG.get_double("robust_zscore_slow",
                                     "min_vol_scalar", 0.7) *
                                 common::kSignalScale),
            static_cast<int64_t>(INI_CONFIG.get_double("robust_zscore_slow",
                                     "max_vol_scalar", 1.3) *
                                 common::kSignalScale),
            static_cast<int64_t>(INI_CONFIG.get_double("robust_zscore_slow",
                                     "vol_ratio_low", 0.5) *
                                 common::kSignalScale),
            static_cast<int64_t>(INI_CONFIG.get_double("robust_zscore_slow",
                                     "vol_ratio_high", 2.0) *
                                 common::kSignalScale),
            INI_CONFIG.get_int("robust_zscore_slow", "baseline_min_history",
                30)})),

        // === Adverse selection tracking ===
        original_safety_margin_bps_(entry_cfg_.safety_margin_bps) {
    this->logger_.info(
        "[MeanReversionMaker] Initialized | min_quantity:{} raw | "
        "simultaneous:{}",
        dynamic_threshold_->get_min_quantity(),
        allow_simultaneous_positions_);
  }

  // ========================================
  // 100ms interval: Orderbook update
  // ========================================
  void on_orderbook_updated(const common::TickerId& ticker, common::PriceType,
      common::Side, const MarketOrderBookT* order_book) noexcept {
    ticker_ = ticker;
    uint64_t current_time = get_current_time_ns();

    // Throttle to ~100ms interval
    constexpr uint64_t THROTTLE_NS = 100'000'000;  // 100ms
    if (current_time - last_orderbook_check_time_ < THROTTLE_NS) {
      return;
    }
    last_orderbook_check_time_ = current_time;

    // === 1. Update orderbook threshold (100ms interval) ===
    dynamic_threshold_->update_orderbook_threshold(order_book);

    // === 2. Calculate final threshold ===
    current_wall_threshold_raw_ =
        dynamic_threshold_->calculate(order_book, current_time);

    // === 3. Detect walls (bidirectional) ===
    const int min_price_int = order_book->config().min_price_int;
    // Detect walls (for reference, not for gating entry)
    bid_wall_info_ = this->feature_engine_->detect_wall(order_book,
        common::Side::kBuy,
        wall_cfg_.max_levels,
        current_wall_threshold_raw_,
        wall_cfg_.max_distance_bps,
        min_price_int,
        wall_level_qty_,
        wall_level_idx_);

    ask_wall_info_ = this->feature_engine_->detect_wall(order_book,
        common::Side::kSell,
        wall_cfg_.max_levels,
        current_wall_threshold_raw_,
        wall_cfg_.max_distance_bps,
        min_price_int,
        wall_level_qty_,
        wall_level_idx_);

    // === 3.5. Update wall quality trackers (spoofing detection) ===
    if (bid_wall_info_.is_valid) {
      bid_wall_tracker_.update(current_time,
          bid_wall_info_.accumulated_notional,
          bid_wall_info_.distance_bps);
    } else {
      bid_wall_tracker_.reset();
    }

    if (ask_wall_info_.is_valid) {
      ask_wall_tracker_.update(current_time,
          ask_wall_info_.accumulated_notional,
          ask_wall_info_.distance_bps);
    } else {
      ask_wall_tracker_.reset();
    }

    // === 3.6. Update market regime (lightweight, throttled to 100ms) ===
    const auto* bbo = order_book->get_bbo();
    int64_t mid_price = (bbo->bid_price.value + bbo->ask_price.value) / 2;
    int64_t z_slow = robust_zscore_slow_->calculate_zscore(mid_price);
    update_market_regime(z_slow);

    // NOTE: Wall detection does NOT gate entry anymore
    // Entry is now gated by mean reversion state (REVERSAL_DETECTED)
    // Wall is checked AFTER reversal is detected

    // === 4. Position exit monitoring (stop loss) ===
    check_position_exit(order_book);
  }

  // ========================================
  // Realtime: Trade update
  // ========================================
  void on_trade_updated(const MarketData* market_data,
      MarketOrderBookT* order_book) noexcept {
    const auto* current_bbo = order_book->get_bbo();

    // BBO validation
    if (!is_bbo_valid(current_bbo)) {
      this->logger_.warn("Invalid BBO | bid:{}/{} ask:{}/{}",
          current_bbo->bid_price.value,
          current_bbo->bid_qty.value,
          current_bbo->ask_price.value,
          current_bbo->ask_qty.value);
      return;
    }

    // === 1. Hot path: Multi-timeframe Z-score tracking ===
    // Update all timeframes
    robust_zscore_fast_->on_price(market_data->price.value);
    robust_zscore_mid_->on_price(market_data->price.value);
    robust_zscore_slow_->on_price(market_data->price.value);

    // Calculate Z-scores for all timeframes (scaled by kZScoreScale)
    int64_t z_fast =
        robust_zscore_fast_->calculate_zscore(market_data->price.value);
    int64_t z_mid =
        robust_zscore_mid_->calculate_zscore(market_data->price.value);
    int64_t z_slow =
        robust_zscore_slow_->calculate_zscore(market_data->price.value);

    // === 1.1. Adverse Selection Detection (Markout Analysis) ===
    uint64_t now = get_current_time_ns();
    adverse_selection_tracker_.on_price_update(now,
        market_data->price.value,
        adverse_selection_cfg_);

    // Adaptive response: widen safety_margin if being picked off
    if (adverse_selection_tracker_.is_being_picked_off(
            adverse_selection_cfg_)) {
      // margin = original * multiplier / kSignalScale
      const_cast<EntryConfig&>(entry_cfg_).safety_margin_bps =
          (original_safety_margin_bps_ *
              adverse_selection_cfg_.margin_multiplier) /
          common::kSignalScale;

      if (debug_cfg_.log_entry_exit) {
        this->logger_.warn(
            "[Adverse Selection] Being picked off | ratio:{} | "
            "widening margin: {} → {} bps",
            adverse_selection_tracker_.get_ratio(
                adverse_selection_cfg_.min_samples),
            original_safety_margin_bps_,
            entry_cfg_.safety_margin_bps);
      }
    } else {
      // Reset to original if not being picked off
      const_cast<EntryConfig&>(entry_cfg_).safety_margin_bps =
          original_safety_margin_bps_;
    }

    // Multi-timeframe alignment check
    // Long: Fast & Mid oversold, but Slow NOT in strong downtrend
    bool long_timeframe_aligned = (z_fast < -zscore_entry_threshold_) &&
                                  (z_mid < -zscore_entry_threshold_) &&
                                  (z_slow > -zscore_slow_threshold_);

    // Short: Fast & Mid overbought, but Slow NOT in strong uptrend
    bool short_timeframe_aligned = (z_fast > zscore_entry_threshold_) &&
                                   (z_mid > zscore_entry_threshold_) &&
                                   (z_slow < zscore_slow_threshold_);

    // Update mean reversion phase using mid-term Z-score (ALWAYS, regardless of wall)
    update_long_phase(z_mid);
    update_short_phase(z_mid);

    // === 2. Long entry check (Phase-Based Mean Reversion + Multi-Timeframe) ===
    // NEW ORDER: Check reversal signal FIRST, then timeframe alignment, then wall
    if (is_long_reversal_signal(market_data)) {
      // Check timeframe alignment
      if (long_timeframe_aligned) {
        // Check wall AFTER reversal and alignment
        if (bid_wall_info_.is_valid && validate_defense_realtime(market_data,
                                           prev_bbo_,
                                           current_bbo,
                                           common::Side::kBuy)) {
          check_long_entry(market_data, order_book, current_bbo, z_mid);
        } else {
          if (debug_cfg_.log_entry_exit) {
            this->logger_.info(
                "[Entry Skip] Reversal aligned but no wall | z_mid:{} "
                "z_slow:{}",
                z_mid,
                z_slow);
          }
        }
      } else {
        if (debug_cfg_.log_entry_exit) {
          this->logger_.info(
              "[Entry Skip] Reversal detected but timeframes NOT aligned | "
              "z_fast:{} z_mid:{} z_slow:{}",
              z_fast,
              z_mid,
              z_slow);
        }
      }
    }

    // === 3. Short entry check (Phase-Based Mean Reversion + Multi-Timeframe) ===
    // NEW ORDER: Check reversal signal FIRST, then timeframe alignment, then wall
    if (is_short_reversal_signal(market_data)) {
      // Check timeframe alignment
      if (short_timeframe_aligned) {
        // Check wall AFTER reversal and alignment
        if (ask_wall_info_.is_valid && validate_defense_realtime(market_data,
                                           prev_bbo_,
                                           current_bbo,
                                           common::Side::kSell)) {
          check_short_entry(market_data, order_book, current_bbo, z_mid);
        } else {
          if (debug_cfg_.log_entry_exit) {
            this->logger_.info(
                "[Entry Skip] Reversal aligned but no wall | z_mid:{} "
                "z_slow:{}",
                z_mid,
                z_slow);
          }
        }
      } else {
        if (debug_cfg_.log_entry_exit) {
          this->logger_.info(
              "[Entry Skip] Reversal detected but timeframes NOT aligned | "
              "z_fast:{} z_mid:{} z_slow:{}",
              z_fast,
              z_mid,
              z_slow);
        }
      }
    }

    // === 4. Save state for next tick ===
    prev_bbo_ = *current_bbo;
    prev_z_score_ = z_mid;

    // === 5. Cold path: Background updates ===
    // Accumulate trade volume for wall threshold (EMA update)
    // This updates slowly (alpha=0.03) and only used in on_orderbook_updated (100ms)
    uint64_t current_time = get_current_time_ns();
    dynamic_threshold_->on_trade(current_time,
        market_data->price.value,
        market_data->qty.value);

    // === 6. Trigger TTL sweep (every trade) ===
    this->order_manager_->apply({});
  }

  void on_order_updated(const ExecutionReport* report) noexcept {
    // Note: TradeEngine already calls position_keeper_->add_fill(report)
    // Do NOT call it again here to avoid double-counting

    // Only sync position state on FILLED, CANCELED, or REJECTED events
    if (report->ord_status != trading::OrdStatus::kFilled &&
        report->ord_status != trading::OrdStatus::kPartiallyFilled &&
        report->ord_status != trading::OrdStatus::kCanceled &&
        report->ord_status != trading::OrdStatus::kRejected) {
      return;
    }

    // Get current position from PositionKeeper
    auto* pos_info = this->position_keeper_->get_position_info(ticker_);

    // === Handle FILLED: PENDING → ACTIVE (or NONE → ACTIVE for late fills) ===
    if (report->ord_status == trading::OrdStatus::kFilled ||
        report->ord_status == trading::OrdStatus::kPartiallyFilled) {

      // Activate LONG position
      if (report->side == common::Side::kBuy) {
        // Normal case: PENDING → ACTIVE
        if (long_position_.status == PositionStatus::PENDING) {
          // Check if this is the expected order or a late fill
          if (long_position_.pending_order_id &&
              *long_position_.pending_order_id == report->cl_order_id) {
            // Normal fill - expected order
            long_position_.status = PositionStatus::ACTIVE;
            long_position_.entry_price = report->avg_price.value;
            long_position_.entry_wall_info =
                bid_wall_info_;  // Update wall at fill time
            long_position_.state_time = get_current_time_ns();
            long_position_.pending_order_id.reset();

            // Track fill for adverse selection detection
            adverse_selection_tracker_.on_fill(long_position_.state_time,
                report->avg_price.value,
                report->side,
                adverse_selection_cfg_.max_fill_history);

            if (debug_cfg_.log_entry_exit) {
              this->logger_.info(
                  "[Entry Filled] LONG | qty:{} | price:{} | "
                  "wall:{}@{} bps",
                  report->last_qty.value,
                  report->avg_price.value,
                  long_position_.entry_wall_info.accumulated_notional,
                  long_position_.entry_wall_info.distance_bps);
            }
          } else {
            // LATE FILL DETECTED!
            const int64_t actual_position = pos_info->long_position_raw_;

            this->logger_.warn(
                "[LATE FILL DETECTED] LONG | expected_order_id:{} | "
                "actual_order_id:{} | "
                "actual_position:{} | emergency_liquidating",
                long_position_.pending_order_id
                    ? common::toString(*long_position_.pending_order_id)
                    : "none",
                common::toString(report->cl_order_id),
                actual_position);

            if (actual_position > 0) {
              emergency_exit(common::Side::kSell,
                  report->avg_price.value,
                  "Late fill");
            }

            long_position_.status = PositionStatus::NONE;
            long_position_.pending_order_id.reset();
          }
        }
        // Late fill case: NONE → ACTIVE (cancelled order filled after timeout)
        else if (long_position_.status == PositionStatus::NONE &&
                 pos_info->long_position_raw_ > 0) {
          const int64_t actual_position = pos_info->long_position_raw_;

          this->logger_.warn(
              "[LATE FILL DETECTED - No Pending] LONG | order_id:{} | "
              "actual_position:{} | emergency_liquidating",
              common::toString(report->cl_order_id),
              actual_position);

          emergency_exit(common::Side::kSell,
              report->avg_price.value,
              "Late fill - no pending");
          long_position_.status = PositionStatus::NONE;
        }
      }

      // Activate SHORT position
      if (report->side == common::Side::kSell) {
        // Normal case: PENDING → ACTIVE
        if (short_position_.status == PositionStatus::PENDING) {
          // Check if this is the expected order or a late fill
          if (short_position_.pending_order_id &&
              *short_position_.pending_order_id == report->cl_order_id) {
            // Normal fill - expected order
            short_position_.status = PositionStatus::ACTIVE;
            short_position_.entry_price = report->avg_price.value;
            short_position_.entry_wall_info =
                ask_wall_info_;  // Update wall at fill time
            short_position_.state_time = get_current_time_ns();
            short_position_.pending_order_id.reset();

            // Track fill for adverse selection detection
            adverse_selection_tracker_.on_fill(short_position_.state_time,
                report->avg_price.value,
                report->side,
                adverse_selection_cfg_.max_fill_history);

            if (debug_cfg_.log_entry_exit) {
              this->logger_.info(
                  "[Entry Filled] SHORT | qty:{} | price:{} | "
                  "wall:{}@{} bps",
                  report->last_qty.value,
                  report->avg_price.value,
                  short_position_.entry_wall_info.accumulated_notional,
                  short_position_.entry_wall_info.distance_bps);
            }
          } else {
            // LATE FILL DETECTED!
            const int64_t actual_position = pos_info->short_position_raw_;

            this->logger_.warn(
                "[LATE FILL DETECTED] SHORT | expected_order_id:{} | "
                "actual_order_id:{} | "
                "actual_position:{} | emergency_liquidating",
                short_position_.pending_order_id
                    ? common::toString(*short_position_.pending_order_id)
                    : "none",
                common::toString(report->cl_order_id),
                actual_position);

            if (actual_position > 0) {
              emergency_exit(common::Side::kBuy,
                  report->avg_price.value,
                  "Late fill");
            }

            short_position_.status = PositionStatus::NONE;
            short_position_.pending_order_id.reset();
          }
        }
        // Late fill case: NONE → ACTIVE (cancelled order filled after timeout)
        else if (short_position_.status == PositionStatus::NONE &&
                 pos_info->short_position_raw_ > 0) {
          const int64_t actual_position = pos_info->short_position_raw_;

          this->logger_.warn(
              "[LATE FILL DETECTED - No Pending] SHORT | order_id:{} | "
              "actual_position:{} | emergency_liquidating",
              common::toString(report->cl_order_id),
              actual_position);

          emergency_exit(common::Side::kBuy,
              report->avg_price.value,
              "Late fill - no pending");
          short_position_.status = PositionStatus::NONE;
        }
      }
    }

    // === Handle CANCELED/REJECTED: PENDING → NONE ===
    if (report->ord_status == trading::OrdStatus::kCanceled ||
        report->ord_status == trading::OrdStatus::kRejected) {

      // Cancel LONG order
      if (report->side == common::Side::kBuy &&
          long_position_.status == PositionStatus::PENDING) {
        long_position_.status = PositionStatus::NONE;
        if (debug_cfg_.log_entry_exit) {
          this->logger_.info("[Entry Canceled] LONG | reason:{}",
              trading::toString(report->ord_status));
        }
      }

      // Cancel SHORT order
      if (report->side == common::Side::kSell &&
          short_position_.status == PositionStatus::PENDING) {
        short_position_.status = PositionStatus::NONE;
        if (debug_cfg_.log_entry_exit) {
          this->logger_.info("[Entry Canceled] SHORT | reason:{}",
              trading::toString(report->ord_status));
        }
      }
    }

    // === Handle position close: ACTIVE → NONE ===
    if (long_position_.status == PositionStatus::ACTIVE &&
        pos_info->long_position_raw_ == 0) {
      long_position_.status = PositionStatus::NONE;
      long_position_.pending_order_id.reset();    // Clear exit order ID
      long_position_.is_regime_override = false;  // Reset override flag
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info("[Exit Complete] Long closed | PnL: {}",
            pos_info->long_real_pnl_);
      }
    }

    if (short_position_.status == PositionStatus::ACTIVE &&
        pos_info->short_position_raw_ == 0) {
      short_position_.status = PositionStatus::NONE;
      short_position_.pending_order_id.reset();    // Clear exit order ID
      short_position_.is_regime_override = false;  // Reset override flag
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info("[Exit Complete] Short closed | PnL: {}",
            pos_info->short_real_pnl_);
      }
    }
  }

 private:
  // ========================================
  // Defense validation (realtime BBO)
  // ========================================
  bool validate_defense_realtime(const MarketData* trade, const BBO& prev_bbo,
      const BBO* current_bbo, common::Side defense_side) const {
    if (defense_side == common::Side::kBuy) {
      // Long defense: check Bid after sell impact
      bool price_held =
          (current_bbo->bid_price.value == prev_bbo.bid_price.value);
      bool qty_sufficient = (current_bbo->bid_qty.value >=
                             trade->qty.value * defense_qty_multiplier_);

      if (debug_cfg_.log_defense_check) {
        this->logger_.debug(
            "[Defense] Long | trade_qty:{}, prev_bid:{}/{}, curr_bid:{}/{}, "
            "result:{}",
            trade->qty.value,
            prev_bbo.bid_price.value,
            prev_bbo.bid_qty.value,
            current_bbo->bid_price.value,
            current_bbo->bid_qty.value,
            price_held && qty_sufficient);
      }

      return price_held && qty_sufficient;

    } else {
      // Short defense: check Ask after buy impact
      bool price_held =
          (current_bbo->ask_price.value == prev_bbo.ask_price.value);
      bool qty_sufficient = (current_bbo->ask_qty.value >=
                             trade->qty.value * defense_qty_multiplier_);

      if (debug_cfg_.log_defense_check) {
        this->logger_.debug(
            "[Defense] Short | trade_qty:{}, prev_ask:{}/{}, curr_ask:{}/{}, "
            "result:{}",
            trade->qty.value,
            prev_bbo.ask_price.value,
            prev_bbo.ask_qty.value,
            current_bbo->ask_price.value,
            current_bbo->ask_qty.value,
            price_held && qty_sufficient);
      }

      return price_held && qty_sufficient;
    }
  }

  // ========================================
  // OBI calculation (int64_t version)
  // ========================================
  // Returns OBI scaled by kObiScale. Range: [-kObiScale, +kObiScale]
  int64_t calculate_orderbook_imbalance_int64(
      const MarketOrderBookT* order_book) {
    // 1. Extract quantities from orderbook
    (void)order_book->peek_qty(true,
        entry_cfg_.obi_levels,
        std::span<int64_t>(bid_qty_),
        {});
    (void)order_book->peek_qty(false,
        entry_cfg_.obi_levels,
        std::span<int64_t>(ask_qty_),
        {});

    // 2. Use FeatureEngine's optimized OBI calculation (int64_t version)
    return this->feature_engine_->orderbook_imbalance_int64(bid_qty_, ask_qty_);
  }

  // ========================================
  // Reversal momentum check (volume-based)
  // ========================================
  bool check_reversal_momentum(common::Side expected_direction) const {
    if (!reversal_cfg_.enabled) {
      return true;  // Always pass if disabled
    }

    const auto* trades = this->feature_engine_->get_recent_trades();
    const size_t trade_count = this->feature_engine_->get_trade_history_size();

    if (trade_count < static_cast<size_t>(reversal_cfg_.lookback_ticks)) {
      return false;  // Insufficient data
    }

    size_t count = std::min(trade_count,
        static_cast<size_t>(reversal_cfg_.lookback_ticks));
    int directional_count = 0;
    double directional_volume = 0.0;
    double opposite_volume = 0.0;

    // Analyze recent N ticks
    for (size_t i = trade_count - count; i < trade_count; ++i) {
      if (trades[i].side == expected_direction) {
        directional_count++;
        directional_volume += trades[i].qty_raw;
      } else {
        opposite_volume += trades[i].qty_raw;
      }
    }

    // Check 1: Minimum directional ticks (e.g., 3 out of 5 = 60%)
    bool tick_condition =
        (directional_count >= reversal_cfg_.min_directional_ticks);

    // Check 2: Volume ratio (e.g., sell volume > buy volume * 1.2)
    bool volume_condition =
        (directional_volume > opposite_volume * reversal_cfg_.min_volume_ratio);

    return tick_condition && volume_condition;
  }

  // ========================================
  // Reversal momentum check for EXIT (stricter than entry)
  // ========================================
  bool check_reversal_momentum_exit(common::Side opposite_direction) const {
    if (!exit_cfg_.reversal_momentum_exit) {
      return false;  // Disabled
    }

    const auto* trades = this->feature_engine_->get_recent_trades();
    const size_t trade_count = this->feature_engine_->get_trade_history_size();

    if (trade_count < static_cast<size_t>(exit_cfg_.exit_lookback_ticks)) {
      return false;  // Insufficient data
    }

    size_t count = std::min(trade_count,
        static_cast<size_t>(exit_cfg_.exit_lookback_ticks));
    int opposite_count = 0;
    double opposite_volume = 0.0;
    double current_volume = 0.0;

    // Analyze recent N ticks for opposite direction pressure
    for (size_t i = trade_count - count; i < trade_count; ++i) {
      if (trades[i].side == opposite_direction) {
        opposite_count++;
        opposite_volume += trades[i].qty_raw;
      } else {
        current_volume += trades[i].qty_raw;
      }
    }

    // Stricter than entry: 70% ticks, 1.5x volume (vs entry 50%, 1.3x)
    bool tick_condition =
        (opposite_count >= exit_cfg_.exit_min_directional_ticks);
    bool volume_condition =
        (opposite_volume > current_volume * exit_cfg_.exit_min_volume_ratio);

    return tick_condition && volume_condition;
  }

  // ========================================
  // Long entry
  // ========================================
  void check_long_entry(const MarketData* trade, MarketOrderBookT* order_book,
      const BBO* bbo, int64_t z_robust) {
    // Z-score is passed as parameter to avoid redundant calculation
    // z_robust is scaled by kZScoreScale (10000). -2.5 = -25000

    if (debug_cfg_.log_entry_exit) {
      this->logger_.info("[RobustZ] price:{} | median:{} | MAD:{} | z:{}",
          trade->price.value,
          robust_zscore_mid_->get_median(),
          robust_zscore_mid_->get_mad(),
          z_robust);
    }

    // 1. Market regime filter (avoid counter-trend trades)
    // EXCEPTION: Allow LONG in downtrend if DEEP oversold (z_mid < -25000 = -2.5)
    constexpr int64_t kDeepOversoldThreshold = -25000;  // -2.5 * kZScoreScale
    if (current_regime_ == MarketRegime::TRENDING_DOWN) {
      if (z_robust > kDeepOversoldThreshold) {  // Not deep enough to override
        if (debug_cfg_.log_entry_exit) {
          this->logger_.info(
              "[Entry Block] LONG | Market in DOWNTREND | regime:TRENDING_DOWN "
              "| z_mid:{} (need < -25000 for override)",
              z_robust);
        }
        return;
      } else {
        // Deep oversold override - allow entry despite downtrend
        if (debug_cfg_.log_entry_exit) {
          this->logger_.info(
              "[Regime Override] LONG allowed in DOWNTREND | z_mid:{} < "
              "-25000 (DEEP oversold)",
              z_robust);
        }
      }
    }

    // 2. Wall quality check (spoofing detection)
    double wall_quality = bid_wall_tracker_.composite_quality();

    if (wall_quality < 0.6) {  // 60% minimum quality threshold
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[Entry Block] LONG | Wall quality too low (spoofing?) | "
            "quality:{:.2f} | stability:{:.2f} | persistence:{:.2f} | "
            "distance:{:.2f}",
            wall_quality,
            bid_wall_tracker_.stability_score(),
            bid_wall_tracker_.persistence_score(),
            bid_wall_tracker_.distance_consistency_score());
      }
      return;
    }

    // 2. Calculate Multi-Factor Signal Score
    int64_t obi = calculate_orderbook_imbalance_int64(order_book);
    SignalScore signal =
        calculate_long_signal_score(z_robust, bid_wall_info_, obi);
    int64_t composite = signal.composite(entry_cfg_);

    // Check signal quality threshold
    if (composite < entry_cfg_.min_signal_quality) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[Entry Block] LONG | Signal quality too low | "
            "score:{} < {} | z:{} wall:{} vol:{} obi:{}",
            composite,
            entry_cfg_.min_signal_quality,
            signal.z_score_strength,
            signal.wall_strength,
            signal.volume_strength,
            signal.obi_strength);
      }
      return;
    }

    // 2. Check Z-score threshold (oversold)
    if (z_robust >= -zscore_entry_threshold_)
      return;

    // 3. Wall existence check (CRITICAL)
    if (!bid_wall_info_.is_valid) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info("[Entry Block] Long | No wall | z:{}", z_robust);
      }
      return;
    }

    // 4. OBI check (sell dominance for mean reversion)
    // Mean reversion: enter LONG when sell pressure is WEAKENING (expect bounce)
    // Directional filter: Block if OBI < -threshold (sell momentum still too strong)
    // NOTE: OBI already calculated above for signal scoring
    if (obi >= 0.0) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[Entry Block] Long | OBI not negative | z:{:.2f} | obi:{:.2f}",
            z_robust,
            obi);
      }
      return;
    }
    if (obi < -entry_cfg_.obi_threshold) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[Entry Block] Long | OBI too negative (momentum still down) | "
            "z:{:.2f} | obi:{:.2f} < -{:.2f}",
            z_robust,
            obi,
            entry_cfg_.obi_threshold);
      }
      return;
    }

    // 5.5. Reversal momentum check (buy pressure building?)
    if (!check_reversal_momentum(common::Side::kBuy)) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[Entry Block] Long | Insufficient buy momentum | z:{:.2f}",
            z_robust);
      }
      return;
    }

    // 6. Spread filter (in bps: 10000 = 100%)
    int64_t spread_bps =
        ((bbo->ask_price.value - bbo->bid_price.value) * common::kBpsScale) /
        bbo->bid_price.value;
    if (spread_bps < entry_cfg_.min_spread_bps) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[Entry Block] Long | Spread too small | z:{} | spread:{} bps "
            "< {} bps",
            z_robust,
            spread_bps,
            entry_cfg_.min_spread_bps);
      }
      return;
    }

    // 7. Set position to PENDING state BEFORE sending order
    long_position_.status = PositionStatus::PENDING;
    long_position_.qty = entry_cfg_.position_size_raw;
    long_position_.entry_price = bbo->bid_price.value;
    long_position_.entry_wall_info = bid_wall_info_;
    long_position_.state_time = get_current_time_ns();
    long_position_.is_regime_override =
        (current_regime_ == MarketRegime::TRENDING_DOWN);

    // 8. Execute entry (OrderId stored internally)
    place_entry_order(common::Side::kBuy, bbo->bid_price.value);

    if (debug_cfg_.log_entry_exit) {
      this->logger_.info(
          "[Entry Signal] LONG | quality:{} ({}) | wall_quality:{} | "
          "z_robust:{} | "
          "price:{} | wall:{}@{} bps | obi:{} | "
          "components: z={} wall={} vol={} obi={}",
          composite,
          signal.get_quality(entry_cfg_) == SignalScore::Quality::EXCELLENT
              ? "EXCELLENT"
              : "GOOD",
          wall_quality,
          z_robust,
          bbo->bid_price.value,
          bid_wall_info_.accumulated_notional,
          bid_wall_info_.distance_bps,
          obi,
          signal.z_score_strength,
          signal.wall_strength,
          signal.volume_strength,
          signal.obi_strength);
    }
  }

  // ========================================
  // Short entry
  // ========================================
  void check_short_entry(const MarketData*, MarketOrderBookT* order_book,
      const BBO* bbo, int64_t z_robust) {
    // Z-score is passed as parameter to avoid redundant calculation
    // z_robust is scaled by kZScoreScale (10000). +2.5 = +25000

    // 1. Market regime filter (avoid counter-trend trades)
    // EXCEPTION: Allow SHORT in uptrend if DEEP overbought (z_mid > +25000 = +2.5)
    constexpr int64_t kDeepOverboughtThreshold = 25000;  // +2.5 * kZScoreScale
    if (current_regime_ == MarketRegime::TRENDING_UP) {
      if (z_robust < kDeepOverboughtThreshold) {  // Not deep enough to override
        if (debug_cfg_.log_entry_exit) {
          this->logger_.info(
              "[Entry Block] SHORT | Market in UPTREND | regime:TRENDING_UP | "
              "z_mid:{} (need > +25000 for override)",
              z_robust);
        }
        return;
      } else {
        // Deep overbought override - allow entry despite uptrend
        if (debug_cfg_.log_entry_exit) {
          this->logger_.info(
              "[Regime Override] SHORT allowed in UPTREND | z_mid:{} > "
              "+25000 (DEEP overbought)",
              z_robust);
        }
      }
    }

    // 2. Wall quality check (spoofing detection)
    double wall_quality = ask_wall_tracker_.composite_quality();

    if (wall_quality < 0.6) {  // 60% minimum quality threshold
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[Entry Block] SHORT | Wall quality too low (spoofing?) | "
            "quality:{:.2f} | stability:{:.2f} | persistence:{:.2f} | "
            "distance:{:.2f}",
            wall_quality,
            ask_wall_tracker_.stability_score(),
            ask_wall_tracker_.persistence_score(),
            ask_wall_tracker_.distance_consistency_score());
      }
      return;
    }

    // 2. Calculate Multi-Factor Signal Score
    int64_t obi = calculate_orderbook_imbalance_int64(order_book);
    SignalScore signal =
        calculate_short_signal_score(z_robust, ask_wall_info_, obi);
    int64_t composite = signal.composite(entry_cfg_);

    // Check signal quality threshold
    if (composite < entry_cfg_.min_signal_quality) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[Entry Block] SHORT | Signal quality too low | "
            "score:{} < {} | z:{} wall:{} vol:{} obi:{}",
            composite,
            entry_cfg_.min_signal_quality,
            signal.z_score_strength,
            signal.wall_strength,
            signal.volume_strength,
            signal.obi_strength);
      }
      return;
    }

    // 2. Check if still in overbought territory (but declining)
    // Allow entry if z > threshold * 0.8 (haven't dropped too much)
    // threshold * 0.8 = threshold * 8 / 10
    if (z_robust * 10 < zscore_entry_threshold_ * 8) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[Entry Block] Short | Already dropped too much | z:{} < {}",
            z_robust,
            (zscore_entry_threshold_ * 8) / 10);
      }
      return;
    }

    // 2. Wall existence check (CRITICAL)
    if (!ask_wall_info_.is_valid) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info("[Entry Block] Short | No wall | z:{:.2f}",
            z_robust);
      }
      return;
    }

    // 4. OBI check (buy dominance for mean reversion)
    // Mean reversion: enter SHORT when buy pressure is WEAKENING (expect drop)
    // Directional filter: Block if OBI > threshold (buy momentum still too strong)
    // NOTE: OBI already calculated above for signal scoring
    if (obi <= 0.0) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[Entry Block] Short | OBI not positive | z:{:.2f} | obi:{:.2f}",
            z_robust,
            obi);
      }
      return;
    }
    if (obi > entry_cfg_.obi_threshold) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[Entry Block] Short | OBI too positive (momentum still up) | "
            "z:{:.2f} | obi:{:.2f} > {:.2f}",
            z_robust,
            obi,
            entry_cfg_.obi_threshold);
      }
      return;
    }

    // 5.5. Reversal momentum check (sell pressure building?)
    if (!check_reversal_momentum(common::Side::kSell)) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[Entry Block] Short | Insufficient sell momentum | z:{:.2f}",
            z_robust);
      }
      return;
    }

    // 6. Spread filter (in bps: 10000 = 100%)
    int64_t spread_bps =
        ((bbo->ask_price.value - bbo->bid_price.value) * common::kBpsScale) /
        bbo->bid_price.value;
    if (spread_bps < entry_cfg_.min_spread_bps) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[Entry Block] Short | Spread too small | z:{} | "
            "spread:{} bps < {} bps",
            z_robust,
            spread_bps,
            entry_cfg_.min_spread_bps);
      }
      return;
    }

    // 7. Set position to PENDING state BEFORE sending order
    short_position_.status = PositionStatus::PENDING;
    short_position_.qty = entry_cfg_.position_size_raw;
    short_position_.entry_price = bbo->ask_price.value;
    short_position_.entry_wall_info = ask_wall_info_;
    short_position_.state_time = get_current_time_ns();
    short_position_.is_regime_override =
        (current_regime_ == MarketRegime::TRENDING_UP);

    // 8. Execute entry (OrderId stored internally)
    place_entry_order(common::Side::kSell, bbo->ask_price.value);

    if (debug_cfg_.log_entry_exit) {
      this->logger_.info(
          "[Entry Signal] SHORT | quality:{} ({}) | wall_quality:{} | "
          "z_robust:{} | "
          "price:{} | wall:{}@{} bps | obi:{} | "
          "components: z={} wall={} vol={} obi={}",
          composite,
          signal.get_quality(entry_cfg_) == SignalScore::Quality::EXCELLENT
              ? "EXCELLENT"
              : "GOOD",
          wall_quality,
          z_robust,
          bbo->ask_price.value,
          ask_wall_info_.accumulated_notional,
          ask_wall_info_.distance_bps,
          obi,
          signal.z_score_strength,
          signal.wall_strength,
          signal.volume_strength,
          signal.obi_strength);
    }
  }

  // ========================================
  // Order execution
  // ========================================
  void place_entry_order(common::Side side, int64_t base_price_raw) {
    QuoteIntentType intent{};
    intent.ticker = ticker_;
    intent.side = side;

    // safety_margin_bps is in basis points, convert to price raw:
    // margin_raw = base_price * margin_bps / kBpsScale
    int64_t margin_raw =
        (base_price_raw * entry_cfg_.safety_margin_bps) / common::kBpsScale;

    if (side == common::Side::kBuy) {
      intent.price = common::PriceType::from_raw(base_price_raw - margin_raw);
      if constexpr (SelectedOeTraits::supports_position_side()) {
        intent.position_side = common::PositionSide::kLong;
      }
    } else {
      intent.price = common::PriceType::from_raw(base_price_raw + margin_raw);
      if constexpr (SelectedOeTraits::supports_position_side()) {
        intent.position_side = common::PositionSide::kShort;
      }
    }

    intent.qty = common::QtyType::from_raw(entry_cfg_.position_size_raw);

    if (debug_cfg_.log_entry_exit) {
      this->logger_.info(
          "[Order Sent] {} | base_price:{} | margin_bps:{} | order_price:{} | "
          "qty:{}",
          side == common::Side::kBuy ? "BUY" : "SELL",
          base_price_raw,
          entry_cfg_.safety_margin_bps,
          intent.price.value().value,
          entry_cfg_.position_size_raw);
    }

    auto order_ids = this->order_manager_->apply({intent});

    // Store OrderId in position state
    if (!order_ids.empty()) {
      if (side == common::Side::kBuy) {
        long_position_.pending_order_id = order_ids[0];
      } else {
        short_position_.pending_order_id = order_ids[0];
      }
    }
  }

  // ========================================
  // Position exit monitoring (100ms)
  // ========================================
  void check_position_exit(const MarketOrderBookT* order_book) {
    const auto* bbo = order_book->get_bbo();

    // Calculate once, use twice (avoid redundant computation)
    int64_t mid_price = (bbo->bid_price.value + bbo->ask_price.value) / 2;
    int64_t current_z = robust_zscore_mid_->calculate_zscore(mid_price);
    int64_t current_obi = calculate_orderbook_imbalance_int64(order_book);

    check_long_exit(bbo, mid_price, current_z, current_obi);
    check_short_exit(bbo, mid_price, current_z, current_obi);
  }

  // ========================================
  // Long position exit
  // ========================================
  void check_long_exit(const BBO* bbo, int64_t mid_price, int64_t current_z,
      int64_t current_obi) {
    if (long_position_.status != PositionStatus::ACTIVE) {
      return;
    }

    // Skip if exit order already pending
    if (long_position_.pending_order_id.has_value()) {
      return;
    }

    bool should_exit = false;
    std::string reason;

    // Priority 1: Wall vanished (emergency)
    if (!bid_wall_info_.is_valid) {
      should_exit = true;
      reason = "Bid wall vanished";
    }

    // Priority 2: Volume reversal (sell pressure resuming)
    else if (check_reversal_momentum_exit(common::Side::kSell)) {
      should_exit = true;
      reason = "Sell pressure resuming";
    }

    // Priority 3: OBI reversal (orderbook turned bearish)
    else if (current_obi < -exit_cfg_.obi_exit_threshold) {
      should_exit = true;
      reason = "OBI bearish reversal";
    }

    // Priority 4: Z-score mean reversion (profit target)
    // Override entries (risky) → tighter exit threshold (-0.5 → -1.0)
    // zscore_exit_threshold is scaled by kZScoreScale
    int64_t exit_threshold =
        long_position_.is_regime_override
            ? exit_cfg_.zscore_exit_threshold * 2  // -5000 → -10000
            : exit_cfg_.zscore_exit_threshold;     // -5000

    if (current_z >= -exit_threshold) {
      should_exit = true;
      reason = long_position_.is_regime_override
                   ? "Z-score mean reversion (OVERRIDE mode - quick exit)"
                   : "Z-score mean reversion";
    }

    // Priority 5: Wall decay
    // current < entry * ratio / kSignalScale
    else if (bid_wall_info_.accumulated_notional * common::kSignalScale <
             long_position_.entry_wall_info.accumulated_notional *
                 exit_cfg_.wall_amount_decay_ratio) {
      should_exit = true;
      reason = "Bid wall decayed";
    }

    // Priority 6: Wall distance expansion
    // current > entry * ratio / kSignalScale
    else if (bid_wall_info_.distance_bps * common::kSignalScale >
             long_position_.entry_wall_info.distance_bps *
                 exit_cfg_.wall_distance_expand_ratio) {
      should_exit = true;
      reason = "Bid wall moved away";
    }

    // Priority 7: Stop loss (bps comparison)
    // (mid - entry) / entry < -max_loss_bps / kBpsScale
    // => (mid - entry) * kBpsScale < -max_loss_bps * entry
    else if ((mid_price - long_position_.entry_price) * common::kBpsScale <
             -exit_cfg_.max_loss_bps * long_position_.entry_price) {
      should_exit = true;
      reason = "Stop loss";
    }

    // Priority 8: Time limit (last resort)
    else if (exit_cfg_.enabled &&
             (get_current_time_ns() - long_position_.state_time) >
                 exit_cfg_.max_hold_time_ns) {
      should_exit = true;
      reason = "Max hold time";
    }

    if (should_exit) {
      // Long exit: SELL at bid (taker sells to existing bids)
      auto order_ids =
          emergency_exit(common::Side::kSell, bbo->bid_price.value, reason);
      if (!order_ids.empty()) {
        long_position_.pending_order_id = order_ids[0];
      }
      // Keep ACTIVE until fill is confirmed (prevent re-entry before exit fills)
    }
  }

  // ========================================
  // Short position exit
  // ========================================
  void check_short_exit(const BBO* bbo, int64_t mid_price, int64_t current_z,
      int64_t current_obi) {
    if (short_position_.status != PositionStatus::ACTIVE) {
      return;
    }

    // Skip if exit order already pending
    if (short_position_.pending_order_id.has_value()) {
      return;
    }

    bool should_exit = false;
    std::string reason;

    // Priority 1: Wall vanished (emergency)
    if (!ask_wall_info_.is_valid) {
      should_exit = true;
      reason = "Ask wall vanished";
    }

    // Priority 2: Volume reversal (buy pressure resuming)
    else if (check_reversal_momentum_exit(common::Side::kBuy)) {
      should_exit = true;
      reason = "Buy pressure resuming";
    }

    // Priority 3: OBI reversal (orderbook turned bullish)
    else if (current_obi > exit_cfg_.obi_exit_threshold) {
      should_exit = true;
      reason = "OBI bullish reversal";
    }

    // Priority 4: Z-score mean reversion (profit target)
    // Override entries (risky) → tighter exit threshold (+0.5 → +1.0)
    // zscore_exit_threshold is scaled by kZScoreScale
    int64_t exit_threshold =
        short_position_.is_regime_override
            ? exit_cfg_.zscore_exit_threshold * 2  // +5000 → +10000
            : exit_cfg_.zscore_exit_threshold;     // +5000

    if (current_z <= exit_threshold) {
      should_exit = true;
      reason = short_position_.is_regime_override
                   ? "Z-score mean reversion (OVERRIDE mode - quick exit)"
                   : "Z-score mean reversion";
    }

    // Priority 5: Wall decay
    // ask_wall * kSignalScale < entry_wall * decay_ratio
    else if (ask_wall_info_.accumulated_notional * common::kSignalScale <
             short_position_.entry_wall_info.accumulated_notional *
                 exit_cfg_.wall_amount_decay_ratio) {
      should_exit = true;
      reason = "Ask wall decayed";
    }

    // Priority 6: Wall distance expansion
    // ask_distance * kSignalScale > entry_distance * expand_ratio
    else if (ask_wall_info_.distance_bps * common::kSignalScale >
             short_position_.entry_wall_info.distance_bps *
                 exit_cfg_.wall_distance_expand_ratio) {
      should_exit = true;
      reason = "Ask wall moved away";
    }

    // Priority 7: Stop loss
    // (entry - mid) * kBpsScale / entry < -max_loss_bps
    else if ((short_position_.entry_price - mid_price) * common::kBpsScale /
                 short_position_.entry_price <
             -exit_cfg_.max_loss_bps) {
      should_exit = true;
      reason = "Stop loss";
    }

    // Priority 8: Time limit (last resort)
    else if (exit_cfg_.enabled &&
             (get_current_time_ns() - short_position_.state_time) >
                 exit_cfg_.max_hold_time_ns) {
      should_exit = true;
      reason = "Max hold time";
    }

    if (should_exit) {
      // Short exit: BUY at ask (taker buys from existing asks)
      auto order_ids =
          emergency_exit(common::Side::kBuy, bbo->ask_price.value, reason);
      if (!order_ids.empty()) {
        short_position_.pending_order_id = order_ids[0];
      }
      // Keep ACTIVE until fill is confirmed (prevent re-entry before exit fills)
    }
  }

  // ========================================
  // Emergency exit
  // ========================================
  std::vector<common::OrderId> emergency_exit(common::Side exit_side,
      int64_t market_price_raw, const std::string& reason) {
    QuoteIntentType intent{};
    intent.ticker = ticker_;
    intent.side = exit_side;

    if (exit_side == common::Side::kSell) {
      intent.qty = common::QtyType::from_raw(long_position_.qty);
    } else {
      intent.qty = common::QtyType::from_raw(short_position_.qty);
    }

    // Taker mode
    intent.price = common::PriceType::from_raw(market_price_raw);

    if constexpr (SelectedOeTraits::supports_position_side()) {
      intent.position_side = (exit_side == common::Side::kSell)
                                 ? common::PositionSide::kLong
                                 : common::PositionSide::kShort;
    }

    auto order_ids = this->order_manager_->apply({intent});

    if (debug_cfg_.log_entry_exit) {
      this->logger_.warn("[{} Exit] {} | price:{}",
          (exit_side == common::Side::kSell) ? "Long" : "Short",
          reason,
          market_price_raw);
    }

    return order_ids;
  }

  // ========================================
  // Helper functions
  // ========================================
  bool is_bbo_valid(const BBO* bbo) const {
    return bbo->bid_qty.value != common::kQtyInvalid &&
           bbo->ask_qty.value != common::kQtyInvalid &&
           bbo->bid_price.value != common::kPriceInvalid &&
           bbo->ask_price.value != common::kPriceInvalid &&
           bbo->ask_price.value >= bbo->bid_price.value;
  }

  uint64_t get_current_time_ns() const {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch())
        .count();
  }

  // ========================================
  // Mean Reversion Signal Detection
  // ========================================
  bool is_long_reversal_signal(const MarketData* trade) const {
    // Phase check: Must be in REVERSAL_STRONG (not REVERSAL_WEAK)
    if (long_phase_ != ReversionPhase::REVERSAL_STRONG) {
      return false;
    }

    // Trade direction check: Buy trade confirms reversal
    if (trade->side != common::Side::kBuy) {
      return false;
    }

    // Position check: No existing position
    if (long_position_.status != PositionStatus::NONE) {
      return false;
    }

    // Simultaneous position check
    if (!allow_simultaneous_positions_ &&
        short_position_.status != PositionStatus::NONE) {
      return false;
    }

    return true;
  }

  bool is_short_reversal_signal(const MarketData* trade) const {
    // Phase check: Must be in REVERSAL_STRONG (not REVERSAL_WEAK)
    if (short_phase_ != ReversionPhase::REVERSAL_STRONG) {
      return false;
    }

    // Trade direction check: Sell trade confirms reversal
    if (trade->side != common::Side::kSell) {
      return false;
    }

    // Position check: No existing position
    if (short_position_.status != PositionStatus::NONE) {
      return false;
    }

    // Simultaneous position check
    if (!allow_simultaneous_positions_ &&
        long_position_.status != PositionStatus::NONE) {
      return false;
    }

    return true;
  }

  // ========================================
  // Mean Reversion Phase Tracking (5-State + Volatility-Adaptive)
  // ========================================
  void update_long_phase(int64_t current_z) {
    // Calculate adaptive threshold (using mid-term timeframe)
    // All values scaled by kZScoreScale (10000)
    int64_t adaptive_threshold =
        robust_zscore_mid_->get_adaptive_threshold(zscore_entry_threshold_);

    int64_t z_abs = std::abs(current_z);

    switch (long_phase_) {
      case ReversionPhase::NEUTRAL:
        // Enter BUILDING_OVERSOLD when crossing neutral zone
        if (current_z < -mean_reversion_cfg_.neutral_zone_threshold) {
          long_phase_ = ReversionPhase::BUILDING_OVERSOLD;
          oversold_min_z_ = current_z;

          if (debug_cfg_.log_entry_exit) {
            this->logger_.info(
                "[MeanReversion] Long BUILDING_OVERSOLD | z:{} | threshold:{}",
                current_z,
                adaptive_threshold);
          }
        }
        break;

      case ReversionPhase::BUILDING_OVERSOLD:
        oversold_min_z_ = std::min(oversold_min_z_, current_z);

        // Enter DEEP_OVERSOLD when crossing deep threshold
        // z_abs > threshold * multiplier / kSignalScale
        if (z_abs * common::kSignalScale >
            adaptive_threshold * mean_reversion_cfg_.deep_multiplier) {
          long_phase_ = ReversionPhase::DEEP_OVERSOLD;

          if (debug_cfg_.log_entry_exit) {
            this->logger_.info(
                "[MeanReversion] Long DEEP_OVERSOLD | z:{} | deep_threshold:{}",
                current_z,
                (adaptive_threshold * mean_reversion_cfg_.deep_multiplier) /
                    common::kSignalScale);
          }
        }
        // Return to NEUTRAL if going back above neutral zone
        else if (current_z > -mean_reversion_cfg_.neutral_zone_threshold) {
          long_phase_ = ReversionPhase::NEUTRAL;
          oversold_min_z_ = 0;

          if (debug_cfg_.log_entry_exit) {
            this->logger_.info("[MeanReversion] Long reset to NEUTRAL | z:{}",
                current_z);
          }
        }
        break;

      case ReversionPhase::DEEP_OVERSOLD:
        oversold_min_z_ = std::min(oversold_min_z_, current_z);

        // Check for reversal bounce
        if (current_z >
            oversold_min_z_ + mean_reversion_cfg_.min_reversal_bounce) {
          // Weak reversal: still below weak threshold
          if (z_abs * common::kSignalScale >
              adaptive_threshold *
                  mean_reversion_cfg_.reversal_weak_multiplier) {
            long_phase_ = ReversionPhase::REVERSAL_WEAK;

            if (debug_cfg_.log_entry_exit) {
              this->logger_.info(
                  "[MeanReversion] Long REVERSAL_WEAK | "
                  "min_z:{} -> current_z:{} | bounce:{}",
                  oversold_min_z_,
                  current_z,
                  current_z - oversold_min_z_);
            }
          }
          // Strong reversal: crossed above weak threshold
          else {
            long_phase_ = ReversionPhase::REVERSAL_STRONG;

            if (debug_cfg_.log_entry_exit) {
              this->logger_.info(
                  "[MeanReversion] Long REVERSAL_STRONG | "
                  "min_z:{} -> current_z:{} | bounce:{} | wall:{}",
                  oversold_min_z_,
                  current_z,
                  current_z - oversold_min_z_,
                  bid_wall_info_.is_valid ? "YES" : "NO");
            }
          }
        }
        // Dropped back to BUILDING level
        else if (z_abs * common::kSignalScale <
                 adaptive_threshold * mean_reversion_cfg_.deep_multiplier) {
          long_phase_ = ReversionPhase::BUILDING_OVERSOLD;

          if (debug_cfg_.log_entry_exit) {
            this->logger_.info("[MeanReversion] Long back to BUILDING | z:{}",
                current_z);
          }
        }
        break;

      case ReversionPhase::REVERSAL_WEAK:
        // Re-check threshold (adaptive_threshold may have changed!)
        if (z_abs * common::kSignalScale <
            adaptive_threshold * mean_reversion_cfg_.reversal_weak_multiplier) {
          long_phase_ = ReversionPhase::REVERSAL_STRONG;

          if (debug_cfg_.log_entry_exit) {
            this->logger_.info(
                "[MeanReversion] Long WEAK -> STRONG | z:{} | threshold:{}",
                current_z,
                (adaptive_threshold *
                    mean_reversion_cfg_.reversal_weak_multiplier) /
                    common::kSignalScale);
          }
        }
        // Falling back to DEEP_OVERSOLD
        // current_z < min_z - bounce * ratio / kSignalScale
        else if (current_z * common::kSignalScale <
                 oversold_min_z_ * common::kSignalScale -
                     mean_reversion_cfg_.min_reversal_bounce *
                         mean_reversion_cfg_.false_reversal_ratio) {
          long_phase_ = ReversionPhase::DEEP_OVERSOLD;
          oversold_min_z_ = current_z;

          if (debug_cfg_.log_entry_exit) {
            this->logger_.info(
                "[MeanReversion] Long WEAK -> DEEP (false reversal) | z:{}",
                current_z);
          }
        }
        // Return to neutral
        else if (current_z > -mean_reversion_cfg_.neutral_zone_threshold) {
          long_phase_ = ReversionPhase::NEUTRAL;
          oversold_min_z_ = 0;
        }
        break;

      case ReversionPhase::REVERSAL_STRONG:
        // Only allow entry from this state
        // Reset after entry or return to neutral
        if (long_position_.status != PositionStatus::NONE ||
            current_z > -mean_reversion_cfg_.neutral_zone_threshold) {
          long_phase_ = ReversionPhase::NEUTRAL;
          oversold_min_z_ = 0;

          if (debug_cfg_.log_entry_exit) {
            this->logger_.info(
                "[MeanReversion] Long reset | z:{} | position:{}",
                current_z,
                long_position_.status == PositionStatus::NONE ? "NONE"
                                                              : "ACTIVE");
          }
        }
        // Falling back to WEAK (reversal weakening)
        else if (z_abs * common::kSignalScale >
                 adaptive_threshold *
                     mean_reversion_cfg_.reversal_weak_multiplier) {
          long_phase_ = ReversionPhase::REVERSAL_WEAK;

          if (debug_cfg_.log_entry_exit) {
            this->logger_.info(
                "[MeanReversion] Long STRONG -> WEAK (reversal weakening) | "
                "z:{}",
                current_z);
          }
        }
        break;
    }
  }

  void update_short_phase(int64_t current_z) {
    // Calculate adaptive threshold (using mid-term timeframe)
    // All values scaled by kZScoreScale (10000)
    int64_t adaptive_threshold =
        robust_zscore_mid_->get_adaptive_threshold(zscore_entry_threshold_);

    int64_t z_abs = std::abs(current_z);

    switch (short_phase_) {
      case ReversionPhase::NEUTRAL:
        // Enter BUILDING (overbought) when crossing neutral zone
        if (current_z > mean_reversion_cfg_.neutral_zone_threshold) {
          short_phase_ =
              ReversionPhase::BUILDING_OVERSOLD;  // Reusing for overbought
          overbought_max_z_ = current_z;

          if (debug_cfg_.log_entry_exit) {
            this->logger_.info(
                "[MeanReversion] Short BUILDING_OVERBOUGHT | z:{} | "
                "threshold:{}",
                current_z,
                adaptive_threshold);
          }
        }
        break;

      case ReversionPhase::BUILDING_OVERSOLD:  // Actually overbought for Short
        overbought_max_z_ = std::max(overbought_max_z_, current_z);

        // Enter DEEP_OVERBOUGHT when crossing deep threshold
        if (z_abs * common::kSignalScale >
            adaptive_threshold * mean_reversion_cfg_.deep_multiplier) {
          short_phase_ =
              ReversionPhase::DEEP_OVERSOLD;  // Reusing for deep overbought

          if (debug_cfg_.log_entry_exit) {
            this->logger_.info(
                "[MeanReversion] Short DEEP_OVERBOUGHT | z:{} | "
                "deep_threshold:{}",
                current_z,
                (adaptive_threshold * mean_reversion_cfg_.deep_multiplier) /
                    common::kSignalScale);
          }
        }
        // Return to NEUTRAL
        else if (current_z < mean_reversion_cfg_.neutral_zone_threshold) {
          short_phase_ = ReversionPhase::NEUTRAL;
          overbought_max_z_ = 0;

          if (debug_cfg_.log_entry_exit) {
            this->logger_.info("[MeanReversion] Short reset to NEUTRAL | z:{}",
                current_z);
          }
        }
        break;

      case ReversionPhase::DEEP_OVERSOLD:  // Actually deep overbought for Short
        overbought_max_z_ = std::max(overbought_max_z_, current_z);

        // Check for reversal drop
        if (current_z <
            overbought_max_z_ - mean_reversion_cfg_.min_reversal_bounce) {
          // Weak reversal: still above weak threshold
          if (z_abs * common::kSignalScale >
              adaptive_threshold *
                  mean_reversion_cfg_.reversal_weak_multiplier) {
            short_phase_ = ReversionPhase::REVERSAL_WEAK;

            if (debug_cfg_.log_entry_exit) {
              this->logger_.info(
                  "[MeanReversion] Short REVERSAL_WEAK | "
                  "max_z:{} -> current_z:{} | drop:{}",
                  overbought_max_z_,
                  current_z,
                  overbought_max_z_ - current_z);
            }
          }
          // Strong reversal: crossed below weak threshold
          else {
            short_phase_ = ReversionPhase::REVERSAL_STRONG;

            if (debug_cfg_.log_entry_exit) {
              this->logger_.info(
                  "[MeanReversion] Short REVERSAL_STRONG | "
                  "max_z:{} -> current_z:{} | drop:{} | wall:{}",
                  overbought_max_z_,
                  current_z,
                  overbought_max_z_ - current_z,
                  ask_wall_info_.is_valid ? "YES" : "NO");
            }
          }
        }
        // Rose back to BUILDING level
        else if (z_abs * common::kSignalScale <
                 adaptive_threshold * mean_reversion_cfg_.deep_multiplier) {
          short_phase_ = ReversionPhase::BUILDING_OVERSOLD;

          if (debug_cfg_.log_entry_exit) {
            this->logger_.info("[MeanReversion] Short back to BUILDING | z:{}",
                current_z);
          }
        }
        break;

      case ReversionPhase::REVERSAL_WEAK:
        // Re-check threshold (adaptive_threshold may have changed!)
        if (z_abs * common::kSignalScale <
            adaptive_threshold * mean_reversion_cfg_.reversal_weak_multiplier) {
          short_phase_ = ReversionPhase::REVERSAL_STRONG;

          if (debug_cfg_.log_entry_exit) {
            this->logger_.info(
                "[MeanReversion] Short WEAK -> STRONG | z:{} | threshold:{}",
                current_z,
                (adaptive_threshold *
                    mean_reversion_cfg_.reversal_weak_multiplier) /
                    common::kSignalScale);
          }
        }
        // Rising back to DEEP_OVERBOUGHT
        // current_z > max_z + bounce * ratio / kSignalScale
        else if (current_z * common::kSignalScale >
                 overbought_max_z_ * common::kSignalScale +
                     mean_reversion_cfg_.min_reversal_bounce *
                         mean_reversion_cfg_.false_reversal_ratio) {
          short_phase_ = ReversionPhase::DEEP_OVERSOLD;
          overbought_max_z_ = current_z;

          if (debug_cfg_.log_entry_exit) {
            this->logger_.info(
                "[MeanReversion] Short WEAK -> DEEP (false reversal) | z:{}",
                current_z);
          }
        }
        // Return to neutral
        else if (current_z < mean_reversion_cfg_.neutral_zone_threshold) {
          short_phase_ = ReversionPhase::NEUTRAL;
          overbought_max_z_ = 0;
        }
        break;

      case ReversionPhase::REVERSAL_STRONG:
        // Only allow entry from this state
        // Reset after entry or return to neutral
        if (short_position_.status != PositionStatus::NONE ||
            current_z < mean_reversion_cfg_.neutral_zone_threshold) {
          short_phase_ = ReversionPhase::NEUTRAL;
          overbought_max_z_ = 0;

          if (debug_cfg_.log_entry_exit) {
            this->logger_.info(
                "[MeanReversion] Short reset | z:{} | position:{}",
                current_z,
                short_position_.status == PositionStatus::NONE ? "NONE"
                                                               : "ACTIVE");
          }
        }
        // Rising back to WEAK (reversal weakening)
        else if (z_abs * common::kSignalScale >
                 adaptive_threshold *
                     mean_reversion_cfg_.reversal_weak_multiplier) {
          short_phase_ = ReversionPhase::REVERSAL_WEAK;

          if (debug_cfg_.log_entry_exit) {
            this->logger_.info(
                "[MeanReversion] Short STRONG -> WEAK (reversal weakening) | "
                "z:{}",
                current_z);
          }
        }
        break;
    }
  }

  // ========================================
  // Lightweight Market Regime Detection
  // ========================================

  /**
   * @brief Detect market regime using existing Z-score data (zero overhead)
   *
   * Strategy:
   * 1. Trend detection: 3 consecutive slow Z-scores in same direction
   * 2. Volatility: MAD ratio from robust_zscore_mid
   * 3. Updated every 100ms (throttled, not hot path)
   */
  void update_market_regime(int64_t z_slow) {
    // Track last 3 slow Z-scores (scaled by kZScoreScale)
    z_slow_history_.push_back(z_slow);
    if (z_slow_history_.size() > 3) {
      z_slow_history_.pop_front();
    }

    if (z_slow_history_.size() < 3) {
      current_regime_ = MarketRegime::RANGING;
      return;
    }

    // Volatility ratio (already computed in robust_zscore_mid)
    int64_t baseline_mad = robust_zscore_mid_->get_mad();
    int64_t current_mad = robust_zscore_mid_->get_mad();
    // vol_ratio = current / baseline * kSignalScale
    vol_ratio_ = (baseline_mad > 0)
                     ? (current_mad * common::kSignalScale) / baseline_mad
                     : common::kSignalScale;

    // High volatility override (2.0 * kSignalScale = 20000)
    constexpr int64_t kHighVolThreshold = 20000;
    if (vol_ratio_ > kHighVolThreshold) {
      current_regime_ = MarketRegime::VOLATILE;
      return;
    }

    // Trend detection: 3 consecutive Z-scores below -15000 or above +15000 (±1.5)
    constexpr int64_t kTrendThreshold = 15000;  // 1.5 * kZScoreScale
    bool all_oversold = (z_slow_history_[0] < -kTrendThreshold) &&
                        (z_slow_history_[1] < -kTrendThreshold) &&
                        (z_slow_history_[2] < -kTrendThreshold);

    bool all_overbought = (z_slow_history_[0] > kTrendThreshold) &&
                          (z_slow_history_[1] > kTrendThreshold) &&
                          (z_slow_history_[2] > kTrendThreshold);

    if (all_oversold) {
      current_regime_ = MarketRegime::TRENDING_DOWN;
    } else if (all_overbought) {
      current_regime_ = MarketRegime::TRENDING_UP;
    } else {
      current_regime_ = MarketRegime::RANGING;
    }
  }

  // ========================================
  // Multi-Factor Signal Scoring
  // ========================================

  /**
   * @brief Calculate volume reversal score (int64_t version)
   * @param expected_direction Expected trade direction for reversal
   * @return Normalized score [0, kSignalScale] combining tick ratio and volume ratio
   */
  int64_t calculate_volume_reversal_score(
      common::Side expected_direction) const {
    const auto* trades = this->feature_engine_->get_recent_trades();
    const size_t trade_count = this->feature_engine_->get_trade_history_size();

    const int lookback = entry_cfg_.volume_score_lookback;
    if (trade_count < static_cast<size_t>(lookback))
      return 0;

    int directional_count = 0;
    int64_t directional_volume = 0;
    int64_t total_volume = 0;

    // Analyze recent trades (qty is already int64_t raw)
    for (size_t i = trade_count - static_cast<size_t>(lookback);
        i < trade_count;
        ++i) {
      if (trades[i].side == expected_direction) {
        directional_count++;
        directional_volume += trades[i].qty_raw;
      }
      total_volume += trades[i].qty_raw;
    }

    if (total_volume == 0)
      return 0;

    // Combine tick ratio and volume ratio (both scaled by kSignalScale)
    // tick_ratio = directional_count * kSignalScale / lookback
    int64_t tick_ratio =
        (static_cast<int64_t>(directional_count) * common::kSignalScale) /
        lookback;
    // volume_ratio = directional_volume * kSignalScale / total_volume
    int64_t volume_ratio =
        (directional_volume * common::kSignalScale) / total_volume;

    // Average of both: (tick + volume) / 2
    return (tick_ratio + volume_ratio) / 2;
  }

  /**
   * @brief Calculate long entry signal score
   * @param z Z-score value (scaled by kZScoreScale)
   * @param wall Wall information
   * @param obi Orderbook imbalance (scaled by kObiScale)
   * @return SignalScore with all components normalized to [0, kSignalScale]
   */
  SignalScore calculate_long_signal_score(int64_t z,
      const FeatureEngineT::WallInfo& wall, int64_t obi) const {
    SignalScore score;

    // === 1. Z-score component: normalize to [0, kSignalScale] ===
    // z is scaled by kZScoreScale. Example: z=-25000 (-2.5)
    // Range: [zscore_norm_min, zscore_norm_max] → [0, kSignalScale]
    int64_t z_abs = std::abs(z);
    int64_t z_range = entry_cfg_.zscore_norm_max - entry_cfg_.zscore_norm_min;
    if (z_range > 0) {
      int64_t z_normalized =
          (z_abs - entry_cfg_.zscore_norm_min) * common::kSignalScale / z_range;
      score.z_score_strength =
          std::clamp(z_normalized, int64_t{0}, common::kSignalScale);
    }

    // === 2. Wall strength: compare to dynamic threshold ===
    // wall_target = min_qty * multiplier / kSignalScale
    // strength = wall_notional * kSignalScale / wall_target
    int64_t wall_target = (dynamic_threshold_->get_min_quantity() *
                              entry_cfg_.wall_norm_multiplier) /
                          common::kSignalScale;
    if (wall_target > 0) {
      int64_t wall_normalized =
          (wall.accumulated_notional * common::kSignalScale) / wall_target;
      score.wall_strength =
          std::clamp(wall_normalized, int64_t{0}, common::kSignalScale);
    }

    // === 3. Volume reversal: calculate directional strength ===
    score.volume_strength = calculate_volume_reversal_score(common::Side::kBuy);

    // === 4. OBI strength: normalize to [0, kSignalScale] ===
    // obi is scaled by kObiScale. Range: [obi_norm_min, obi_norm_max]
    int64_t obi_abs = std::abs(obi);
    int64_t obi_range = entry_cfg_.obi_norm_max - entry_cfg_.obi_norm_min;
    if (obi_range > 0) {
      int64_t obi_normalized = (obi_abs - entry_cfg_.obi_norm_min) *
                               common::kSignalScale / obi_range;
      score.obi_strength =
          std::clamp(obi_normalized, int64_t{0}, common::kSignalScale);
    }

    return score;
  }

  /**
   * @brief Calculate short entry signal score
   * @param z Z-score value
   * @param wall Wall information
   * @param obi Orderbook imbalance
   * @return SignalScore with all components normalized to 0-1
   */
  SignalScore calculate_short_signal_score(int64_t z,
      const FeatureEngineT::WallInfo& wall, int64_t obi) const {
    SignalScore score;

    // === 1. Z-score component: normalize to [0, kSignalScale] ===
    int64_t z_abs = std::abs(z);
    int64_t z_range = entry_cfg_.zscore_norm_max - entry_cfg_.zscore_norm_min;
    if (z_range > 0) {
      int64_t z_normalized =
          (z_abs - entry_cfg_.zscore_norm_min) * common::kSignalScale / z_range;
      score.z_score_strength =
          std::clamp(z_normalized, int64_t{0}, common::kSignalScale);
    }

    // === 2. Wall strength: compare to dynamic threshold ===
    int64_t wall_target = (dynamic_threshold_->get_min_quantity() *
                              entry_cfg_.wall_norm_multiplier) /
                          common::kSignalScale;
    if (wall_target > 0) {
      int64_t wall_normalized =
          (wall.accumulated_notional * common::kSignalScale) / wall_target;
      score.wall_strength =
          std::clamp(wall_normalized, int64_t{0}, common::kSignalScale);
    }

    // === 3. Volume reversal ===
    score.volume_strength =
        calculate_volume_reversal_score(common::Side::kSell);

    // === 4. OBI strength: normalize to [0, kSignalScale] ===
    // Short: OBI should be positive (buy pressure) but not too extreme
    int64_t obi_abs = std::abs(obi);
    int64_t obi_range = entry_cfg_.obi_norm_max - entry_cfg_.obi_norm_min;
    if (obi_range > 0) {
      int64_t obi_normalized = (obi_abs - entry_cfg_.obi_norm_min) *
                               common::kSignalScale / obi_range;
      score.obi_strength =
          std::clamp(obi_normalized, int64_t{0}, common::kSignalScale);
    }

    return score;
  }

  // ========================================
  // Member variables (int64_t version)
  // ========================================
  // Config parameters (grouped)
  const bool allow_simultaneous_positions_;
  const int64_t defense_qty_multiplier_;  // scaled by kSignalScale
  const int64_t zscore_entry_threshold_;  // scaled by kZScoreScale

  const WallDetectionConfig wall_cfg_;
  const EntryConfig entry_cfg_;
  const ExitConfig exit_cfg_;
  const ReversalMomentumConfig reversal_cfg_;
  const DebugLoggingConfig debug_cfg_;
  const MeanReversionConfig mean_reversion_cfg_;
  const AdverseSelectionConfig adverse_selection_cfg_;

  // Z-score config (kept separate for module initialization)
  const int zscore_window_size_;
  const int zscore_min_samples_;
  const int64_t zscore_min_mad_threshold_raw_;  // scaled by kPriceScale

  // Multi-timeframe Z-score config
  const int zscore_fast_window_;
  const int zscore_fast_min_samples_;
  const int zscore_slow_window_;
  const int zscore_slow_min_samples_;
  const int64_t zscore_slow_threshold_;  // scaled by kZScoreScale

  // Dynamic state
  common::TickerId ticker_;
  FeatureEngineT::WallInfo bid_wall_info_;
  FeatureEngineT::WallInfo ask_wall_info_;
  FeatureEngineT::WallTracker
      bid_wall_tracker_;  // Wall quality tracking (spoofing detection)
  FeatureEngineT::WallTracker
      ask_wall_tracker_;  // Wall quality tracking (spoofing detection)
  PositionState long_position_;
  PositionState short_position_;
  BBO prev_bbo_;

  // OBI calculation buffers (int64_t)
  std::vector<int64_t> bid_qty_;
  std::vector<int64_t> ask_qty_;

  // Wall detection buffers (reused to avoid allocation)
  std::vector<int64_t> wall_level_qty_;
  std::vector<int> wall_level_idx_;

  // Dynamic threshold (int64_t)
  int64_t current_wall_threshold_raw_;
  std::unique_ptr<DynamicWallThreshold> dynamic_threshold_;

  // Robust Z-score modules (multi-timeframe)
  std::unique_ptr<RobustZScore> robust_zscore_fast_;  // ~1 sec (10 ticks)
  std::unique_ptr<RobustZScore> robust_zscore_mid_;   // ~5 sec (30 ticks)
  std::unique_ptr<RobustZScore> robust_zscore_slow_;  // ~30 sec (100 ticks)

  // Adverse selection tracking
  int64_t original_safety_margin_bps_;  // Backup for restoration
  AdverseSelectionTracker adverse_selection_tracker_;

  // Note: Trade history is now managed by FeatureEngine::recent_trades_

  // Reversal confirmation tracking (int64_t)
  int64_t prev_z_score_{0};  // scaled by kZScoreScale

  // Mean reversion phase tracking (Simplified)
  ReversionPhase long_phase_{ReversionPhase::NEUTRAL};
  ReversionPhase short_phase_{ReversionPhase::NEUTRAL};
  int64_t oversold_min_z_{0};    // Minimum Z-Score reached (scaled)
  int64_t overbought_max_z_{0};  // Maximum Z-Score reached (scaled)

  // Market regime tracking (lightweight, no extra computation)
  MarketRegime current_regime_{MarketRegime::RANGING};
  std::deque<int64_t> z_slow_history_;  // Last 3 slow Z-scores (scaled)
  int64_t vol_ratio_{10000};            // Volatility ratio (kSignalScale)

  // Throttling timestamp for orderbook updates
  uint64_t last_orderbook_check_time_{0};
};

}  // namespace trading

#endif  // MEAN_REVERSION_MAKER_H
