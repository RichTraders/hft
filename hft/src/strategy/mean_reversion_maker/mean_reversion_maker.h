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
  int64_t position_size_raw{10};       // 0.01 * kQtyScale (1000)
  int64_t safety_margin_entry_bps{5};  // 0.0001 = 1 bp
  int64_t safety_margin_exit_bps{5};   // 0.0001 = 1 bp

  // Multi-Factor Scoring parameters (all scaled by kSignalScale=10000)
  int64_t min_signal_quality{6500};  // 0.65 * kSignalScale
  int64_t zscore_weight{3500};       // 0.35 * kSignalScale
  int64_t wall_weight{3000};         // 0.30 * kSignalScale
  int64_t obi_weight{1500};          // 0.15 * kSignalScale

  // Z-score normalization (scaled by kZScoreScale=10000)
  int64_t zscore_norm_min{20000};  // 2.0 * kZScoreScale
  int64_t zscore_norm_max{30000};  // 3.0 * kZScoreScale

  // Wall normalization (scaled by kSignalScale)
  int64_t wall_norm_multiplier{20000};  // 2.0 * kSignalScale

  // OBI normalization (scaled by kObiScale=10000)
  int64_t obi_norm_min{500};   // 0.05 * kObiScale
  int64_t obi_norm_max{2500};  // 0.25 * kObiScale

  // Z-score retention ratio for SHORT entry (0.8 = 80%)
  int64_t short_zscore_min_ratio{8000};  // 0.8 * kSignalScale

  // Defense validation: max price slippage in raw units
  int64_t defense_max_price_slippage_raw{
      500};  // 0.0005 * kPriceScale (5 ticks)
};

struct ExitConfig {
  int64_t wall_amount_decay_ratio{5000};      // 0.5 * kSignalScale
  int64_t wall_distance_expand_ratio{12000};  // 1.2 * kSignalScale
  int64_t max_loss_bps{20};                   // 0.2% = 20 bps
  uint64_t max_hold_time_ns{
      30'000'000'000};                  // 30 seconds (for time_pressure only)
  int64_t max_price_deviation_bps{20};  // 0.2% = 20 bps
  bool cancel_on_wall_decay{true};

  // Active exit conditions (profit-taking)
  int64_t zscore_exit_threshold{5000};  // 0.5 * kZScoreScale
  int64_t obi_exit_threshold{5000};     // 0.5 * kObiScale (0.3 → 0.5)

  // Multi-timeframe exit alignment: neutral zone threshold
  // Exit when all timeframes enter neutral zone (|z| < threshold)
  int64_t exit_neutral_threshold{3000};  // 0.30 * kZScoreScale

  // === Multi-Factor Exit Scoring ===
  // Minimum composite exit score (0.65 = MEDIUM urgency)
  int64_t min_exit_quality{6500};  // 0.65 * kSignalScale

  // Component weights (must sum to kSignalScale=10000)
  int64_t z_reversion_weight{4000};   // 40% (가장 중요)
  int64_t obi_reversal_weight{3000};  // 30%
  int64_t wall_decay_weight{2000};    // 20%
  int64_t time_weight{1000};          // 10%

  // Soft time limit ratio (50% of max_hold_time)
  int64_t soft_time_ratio{5000};  // 0.5 * kSignalScale

  // Override mode: lower exit threshold for risky entries
  int64_t override_exit_threshold{5000};  // 0.5 * kSignalScale

  // Urgency classification thresholds
  int64_t urgency_high_threshold{8000};  // 0.8 * kSignalScale
  int64_t urgency_low_threshold{5000};   // 0.5 * kSignalScale
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
  // Active parameters - scaled by kZScoreScale
  int64_t min_reversal_bounce{2000};      // 0.2 * kZScoreScale
  int64_t neutral_zone_threshold{10000};  // 1.0 * kZScoreScale

  // 5-State threshold multipliers (scaled by kSignalScale)
  int64_t building_multiplier{10000};        // 1.0 * kSignalScale
  int64_t deep_multiplier{12000};            // 1.2 * kSignalScale
  int64_t reversal_weak_multiplier{8000};    // 0.8 * kSignalScale
  int64_t reversal_strong_multiplier{6000};  // 0.6 * kSignalScale

  // False reversal detection (scaled by kSignalScale)
  int64_t false_reversal_ratio{5000};  // 0.5 * kSignalScale
};

struct NormalizationConfig {
  // OBI normalization range (scaled by kObiScale)
  int64_t obi_max_range{10000};  // 1.0 * kObiScale
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
  int64_t obi_strength{0};   // [0, kSignalScale]: Orderbook imbalance alignment

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
// Multi-Factor Exit Scoring (진입 로직과 동일한 패턴)
// ==========================================
/**
 * @brief Exit signal quality score (scaled by kSignalScale=10000)
 *
 * 여러 청산 신호를 정량화하여 복합 점수로 평가
 * - 긴급 청산: 즉시 실행 (벽 소멸, 손절)
 * - 정상 청산: 복합 점수 기반 (이익 실현)
 */
struct ExitScore {
  // === Scored signals [0, kSignalScale] ===
  int64_t z_reversion_strength{0};   // Z-score 회귀 강도
  int64_t obi_reversal_strength{0};  // OBI 반전 강도
  int64_t wall_decay_strength{0};    // 벽 약화 강도
  int64_t time_pressure{0};          // 시간 압박 강도

  /**
   * @brief 복합 청산 점수 계산
   * @param cfg Exit config with component weights
   * @return Composite exit score [0, kSignalScale]
   */
  [[nodiscard]] int64_t composite(const ExitConfig& cfg) const {
    return (cfg.z_reversion_weight * z_reversion_strength +
               cfg.obi_reversal_weight * obi_reversal_strength +
               cfg.wall_decay_weight * wall_decay_strength +
               cfg.time_weight * time_pressure) /
           common::kSignalScale;
  }

  /**
   * @brief 청산 긴급도 분류
   */
  enum class Urgency { HIGH, MEDIUM, LOW, NONE };

  [[nodiscard]] Urgency get_urgency(const ExitConfig& cfg) const {
    int64_t score = composite(cfg);
    if (score >= cfg.urgency_high_threshold)
      return Urgency::HIGH;
    if (score >= cfg.min_exit_quality)
      return Urgency::MEDIUM;
    if (score >= cfg.urgency_low_threshold)
      return Urgency::LOW;
    return Urgency::NONE;
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
  // === Position state structure (int64_t version) ===
  struct PositionState {
    int64_t qty{0};          // Quantity in raw scale (qty * kQtyScale)
    int64_t entry_price{0};  // Entry price in raw scale
    FeatureEngineT::WallInfo entry_wall_info;
    PositionStatus status{PositionStatus::NONE};
    uint64_t state_time{0};  // PENDING: order sent time, ACTIVE: fill time
    std::optional<common::OrderId> pending_order_id;  // Track expected order
  };

  // Note: TradeRecord is now managed by FeatureEngine::TradeInfo

  MeanReversionMakerStrategy(OrderManagerT* order_manager,
      const FeatureEngineT* feature_engine,
      const InventoryManager* inventory_manager,
      PositionKeeper* position_keeper, const common::Logger::Producer& logger,
      const common::TradeEngineCfgHashMap&)
      : BaseStrategy(order_manager, feature_engine, inventory_manager,
            position_keeper, logger),
        // === Defense (int64_t conversion: * kSignalScale) ===
        defense_qty_multiplier_(static_cast<int64_t>(
            INI_CONFIG.get_double("wall_defense", "qty_multiplier", 2.0) *
            common::kSignalScale)),

        // === Z-score thresholds (int64_t conversion: * kZScoreScale) ===
        zscore_mid_threshold_(static_cast<int64_t>(
            INI_CONFIG.get_double("robust_zscore_mid", "entry_threshold", 0.8) *
            common::kZScoreScale)),
        zscore_fast_threshold_(
            static_cast<int64_t>(INI_CONFIG.get_double("robust_zscore_fast",
                                     "entry_threshold", 0.5) *
                                 common::kZScoreScale)),
        zscore_slow_threshold_(
            static_cast<int64_t>(INI_CONFIG.get_double("robust_zscore_slow",
                                     "entry_threshold", 1.4) *
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
                INI_CONFIG.get_double("entry", "safety_margin_entry", 0.0001) *
                common::kBpsScale),
            static_cast<int64_t>(
                INI_CONFIG.get_double("entry", "safety_margin_exit", 0.0001) *
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
            static_cast<int64_t>(
                INI_CONFIG.get_double("entry", "short_zscore_min_ratio", 0.8) *
                common::kSignalScale),
            static_cast<int64_t>(INI_CONFIG.get_double("entry",
                                     "defense_max_price_slippage", 0.0005) *
                                 common::FixedPointConfig::kPriceScale)},

        exit_cfg_{static_cast<int64_t>(INI_CONFIG.get_double("exit",
                                           "wall_amount_decay_ratio", 0.5) *
                                       common::kSignalScale),
            static_cast<int64_t>(INI_CONFIG.get_double("exit",
                                     "wall_distance_expand_ratio", 1.2) *
                                 common::kSignalScale),
            static_cast<int64_t>(
                INI_CONFIG.get_double("exit", "max_loss_pct", 0.002) *
                common::kBpsScale),
            static_cast<uint64_t>(
                INI_CONFIG.get_double("exit", "max_hold_time_sec", 30.0) *
                1'000'000'000),
            static_cast<int64_t>(INI_CONFIG.get_double("exit",
                                     "max_price_deviation_pct", 0.002) *
                                 common::kBpsScale),
            INI_CONFIG.get("exit", "cancel_on_wall_decay", "true") == "true",
            static_cast<int64_t>(
                INI_CONFIG.get_double("exit", "zscore_exit_threshold", 0.5) *
                common::kZScoreScale),
            static_cast<int64_t>(
                INI_CONFIG.get_double("exit", "obi_exit_threshold", 0.5) *
                common::kObiScale),
            static_cast<int64_t>(
                INI_CONFIG.get_double("exit", "exit_neutral_threshold", 0.30) *
                common::kZScoreScale),
            // Multi-Factor Exit Scoring
            static_cast<int64_t>(
                INI_CONFIG.get_double("exit", "min_exit_quality", 0.65) *
                common::kSignalScale),
            static_cast<int64_t>(
                INI_CONFIG.get_double("exit", "z_reversion_weight", 0.40) *
                common::kSignalScale),
            static_cast<int64_t>(
                INI_CONFIG.get_double("exit", "obi_reversal_weight", 0.30) *
                common::kSignalScale),
            static_cast<int64_t>(
                INI_CONFIG.get_double("exit", "wall_decay_weight", 0.20) *
                common::kSignalScale),
            static_cast<int64_t>(
                INI_CONFIG.get_double("exit", "time_weight", 0.10) *
                common::kSignalScale),
            static_cast<int64_t>(
                INI_CONFIG.get_double("exit", "soft_time_ratio", 0.5) *
                common::kSignalScale),
            static_cast<int64_t>(
                INI_CONFIG.get_double("exit", "override_exit_threshold", 0.5) *
                common::kSignalScale),
            static_cast<int64_t>(
                INI_CONFIG.get_double("exit", "urgency_high_threshold", 0.8) *
                common::kSignalScale),
            static_cast<int64_t>(
                INI_CONFIG.get_double("exit", "urgency_low_threshold", 0.5) *
                common::kSignalScale)},

        debug_cfg_{
            INI_CONFIG.get("debug", "log_wall_detection", "false") == "true",
            INI_CONFIG.get("debug", "log_defense_check", "false") == "true",
            INI_CONFIG.get("debug", "log_entry_exit", "false") == "true"},

        mean_reversion_cfg_{
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

        normalization_cfg_{static_cast<int64_t>(
            INI_CONFIG.get_double("normalization", "obi_max_range", 1.0) *
            common::kObiScale)},

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
        zscore_mid_window_(
            INI_CONFIG.get_int("robust_zscore_mid", "window_size", 30)),
        zscore_mid_min_samples_(
            INI_CONFIG.get_int("robust_zscore_mid", "min_samples", 20)),
        zscore_slow_window_(
            INI_CONFIG.get_int("robust_zscore_slow", "window_size", 100)),
        zscore_slow_min_samples_(
            INI_CONFIG.get_int("robust_zscore_slow", "min_samples", 60)),

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
        // Read common parameters once (fallback to robust_zscore section for BTC compatibility)
        robust_zscore_fast_(std::make_unique<RobustZScore>(RobustZScoreConfig{
            zscore_fast_window_,
            zscore_fast_min_samples_,
            static_cast<int64_t>(INI_CONFIG.get_double("robust_zscore_common",
                                     "min_mad_threshold",
                                     INI_CONFIG.get_double("robust_zscore",
                                         "min_mad_threshold", 5.0)) *
                                 common::FixedPointConfig::kPriceScale),
            INI_CONFIG.get_int("robust_zscore_common", "baseline_window",
                INI_CONFIG.get_int("robust_zscore", "baseline_window", 100)),
            static_cast<int64_t>(
                INI_CONFIG.get_double("robust_zscore_common", "min_vol_scalar",
                    INI_CONFIG.get_double("robust_zscore", "min_vol_scalar",
                        0.7)) *
                common::kSignalScale),
            static_cast<int64_t>(
                INI_CONFIG.get_double("robust_zscore_common", "max_vol_scalar",
                    INI_CONFIG.get_double("robust_zscore", "max_vol_scalar",
                        1.3)) *
                common::kSignalScale),
            static_cast<int64_t>(
                INI_CONFIG.get_double("robust_zscore_common", "vol_ratio_low",
                    INI_CONFIG.get_double("robust_zscore", "vol_ratio_low",
                        0.5)) *
                common::kSignalScale),
            static_cast<int64_t>(
                INI_CONFIG.get_double("robust_zscore_common", "vol_ratio_high",
                    INI_CONFIG.get_double("robust_zscore", "vol_ratio_high",
                        2.0)) *
                common::kSignalScale),
            INI_CONFIG.get_int("robust_zscore_common", "baseline_min_history",
                INI_CONFIG.get_int("robust_zscore", "baseline_min_history",
                    30))})),

        robust_zscore_mid_(std::make_unique<RobustZScore>(RobustZScoreConfig{
            zscore_mid_window_,
            zscore_mid_min_samples_,
            static_cast<int64_t>(INI_CONFIG.get_double("robust_zscore_common",
                                     "min_mad_threshold",
                                     INI_CONFIG.get_double("robust_zscore",
                                         "min_mad_threshold", 5.0)) *
                                 common::FixedPointConfig::kPriceScale),
            INI_CONFIG.get_int("robust_zscore_common", "baseline_window",
                INI_CONFIG.get_int("robust_zscore", "baseline_window", 100)),
            static_cast<int64_t>(
                INI_CONFIG.get_double("robust_zscore_common", "min_vol_scalar",
                    INI_CONFIG.get_double("robust_zscore", "min_vol_scalar",
                        0.7)) *
                common::kSignalScale),
            static_cast<int64_t>(
                INI_CONFIG.get_double("robust_zscore_common", "max_vol_scalar",
                    INI_CONFIG.get_double("robust_zscore", "max_vol_scalar",
                        1.3)) *
                common::kSignalScale),
            static_cast<int64_t>(
                INI_CONFIG.get_double("robust_zscore_common", "vol_ratio_low",
                    INI_CONFIG.get_double("robust_zscore", "vol_ratio_low",
                        0.5)) *
                common::kSignalScale),
            static_cast<int64_t>(
                INI_CONFIG.get_double("robust_zscore_common", "vol_ratio_high",
                    INI_CONFIG.get_double("robust_zscore", "vol_ratio_high",
                        2.0)) *
                common::kSignalScale),
            INI_CONFIG.get_int("robust_zscore_common", "baseline_min_history",
                INI_CONFIG.get_int("robust_zscore", "baseline_min_history",
                    30))})),

        robust_zscore_slow_(std::make_unique<RobustZScore>(RobustZScoreConfig{
            zscore_slow_window_,
            zscore_slow_min_samples_,
            static_cast<int64_t>(INI_CONFIG.get_double("robust_zscore_common",
                                     "min_mad_threshold",
                                     INI_CONFIG.get_double("robust_zscore",
                                         "min_mad_threshold", 5.0)) *
                                 common::FixedPointConfig::kPriceScale),
            INI_CONFIG.get_int("robust_zscore_common", "baseline_window",
                INI_CONFIG.get_int("robust_zscore", "baseline_window", 100)),
            static_cast<int64_t>(
                INI_CONFIG.get_double("robust_zscore_common", "min_vol_scalar",
                    INI_CONFIG.get_double("robust_zscore", "min_vol_scalar",
                        0.7)) *
                common::kSignalScale),
            static_cast<int64_t>(
                INI_CONFIG.get_double("robust_zscore_common", "max_vol_scalar",
                    INI_CONFIG.get_double("robust_zscore", "max_vol_scalar",
                        1.3)) *
                common::kSignalScale),
            static_cast<int64_t>(
                INI_CONFIG.get_double("robust_zscore_common", "vol_ratio_low",
                    INI_CONFIG.get_double("robust_zscore", "vol_ratio_low",
                        0.5)) *
                common::kSignalScale),
            static_cast<int64_t>(
                INI_CONFIG.get_double("robust_zscore_common", "vol_ratio_high",
                    INI_CONFIG.get_double("robust_zscore", "vol_ratio_high",
                        2.0)) *
                common::kSignalScale),
            INI_CONFIG.get_int("robust_zscore_common", "baseline_min_history",
                INI_CONFIG.get_int("robust_zscore", "baseline_min_history",
                    30))})),

        // === Adverse selection tracking ===
        original_safety_margin_bps_(entry_cfg_.safety_margin_entry_bps) {
    this->logger_.info("[MeanReversionMaker] Initialized | min_quantity:{} raw",
        dynamic_threshold_->get_min_quantity());
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

    if (debug_cfg_.log_entry_exit) {
      this->logger_.info("[on_trade_updated] price:{} qty:{}",
          market_data->price.value,
          market_data->qty.value);
    }

    // BBO validation
    if (!is_bbo_valid(current_bbo)) {
      this->logger_.warn("Invalid BBO | bid:{}/{} ask:{}/{}",
          current_bbo->bid_price.value,
          current_bbo->bid_qty.value,
          current_bbo->ask_price.value,
          current_bbo->ask_qty.value);
      return;
    }

    if (debug_cfg_.log_entry_exit) {
      this->logger_.info("[BBO valid] bid:{} ask:{}",
          current_bbo->bid_price.value,
          current_bbo->ask_price.value);
    }

    // Calculate multi-timeframe Z-scores
    ZScores zscores =
        calculate_multi_timeframe_zscores(market_data->price.value);

    if (debug_cfg_.log_entry_exit) {
      this->logger_.info("[Z-scores calculated] fast:{} mid:{} slow:{}",
          zscores.z_fast,
          zscores.z_mid,
          zscores.z_slow);
    }

    // Handle adverse selection detection
    handle_adverse_selection(get_current_time_ns(), market_data->price.value);

    // Check timeframe alignments
    bool long_momentum_weak = check_long_momentum_weakening(zscores.z_slow,
        zscores.z_mid,
        zscores.z_fast);
    bool short_momentum_weak = check_short_momentum_weakening(zscores.z_slow,
        zscores.z_mid,
        zscores.z_fast);

    // Try LONG entry if SHORT momentum weakening
    if (short_momentum_weak) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[short_momentum_weak] z_fast:{} z_mid:{} z_slow:{} | bid_wall:{} "
            "| long_pos:{}",
            zscores.z_fast,
            zscores.z_mid,
            zscores.z_slow,
            bid_wall_info_.is_valid,
            static_cast<int>(long_position_.status));
      }

      try_long_entry(market_data, order_book, current_bbo, zscores);
    }

    // Try SHORT entry if LONG momentum weakening
    if (long_momentum_weak) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[long_momentum_weak] z_fast:{} z_mid:{} z_slow:{} | ask_wall:{} | "
            "short_pos:{}",
            zscores.z_fast,
            zscores.z_mid,
            zscores.z_slow,
            ask_wall_info_.is_valid,
            static_cast<int>(short_position_.status));
      }

      try_short_entry(market_data, order_book, current_bbo, zscores);
    }

    if (debug_cfg_.log_entry_exit) {
      this->logger_.info("[on_trade_updated] Completed");
    }

    // Save state for next tick
    prev_bbo_ = *current_bbo;

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

      // Cancel LONG order - reset all position state
      if (report->side == common::Side::kBuy &&
          long_position_.status == PositionStatus::PENDING) {
        long_position_.status = PositionStatus::NONE;
        long_position_.qty = 0;
        long_position_.entry_price = 0;
        long_position_.entry_wall_info = {};
        long_position_.state_time = 0;
        long_position_.pending_order_id.reset();
        if (debug_cfg_.log_entry_exit) {
          this->logger_.info("[Entry Canceled] LONG | reason:{}",
              trading::toString(report->ord_status));
        }
      }

      // Cancel SHORT order - reset all position state
      if (report->side == common::Side::kSell &&
          short_position_.status == PositionStatus::PENDING) {
        short_position_.status = PositionStatus::NONE;
        short_position_.qty = 0;
        short_position_.entry_price = 0;
        short_position_.entry_wall_info = {};
        short_position_.state_time = 0;
        short_position_.pending_order_id.reset();
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
      long_position_.pending_order_id.reset();  // Clear exit order ID
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info("[Exit Complete] Long closed | PnL: {}",
            pos_info->long_real_pnl_);
      }
    }

    if (short_position_.status == PositionStatus::ACTIVE &&
        pos_info->short_position_raw_ == 0) {
      short_position_.status = PositionStatus::NONE;
      short_position_.pending_order_id.reset();  // Clear exit order ID
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
    const int64_t max_price_move = entry_cfg_.defense_max_price_slippage_raw;

    if (defense_side == common::Side::kBuy) {
      // Long defense: check Bid after sell impact
      // Allow bid to drop slightly (up to 2 ticks)
      int64_t price_diff =
          prev_bbo.bid_price.value - current_bbo->bid_price.value;
      bool price_ok = (price_diff <= max_price_move);

      // defense_qty_multiplier_ is scaled by kSignalScale, need to divide
      int64_t required_qty =
          (trade->qty.value * defense_qty_multiplier_) / common::kSignalScale;
      bool qty_sufficient = (current_bbo->bid_qty.value >= required_qty);

      if (debug_cfg_.log_defense_check) {
        this->logger_.debug(
            "[Defense] Long | trade_qty:{}, prev_bid:{}/{}, curr_bid:{}/{}, "
            "price_diff:{} (max:{}), result:{}",
            trade->qty.value,
            prev_bbo.bid_price.value,
            prev_bbo.bid_qty.value,
            current_bbo->bid_price.value,
            current_bbo->bid_qty.value,
            price_diff,
            max_price_move,
            price_ok && qty_sufficient);
      }

      return price_ok && qty_sufficient;

    } else {
      // Short defense: check Ask after buy impact
      // Allow ask to rise slightly (up to 2 ticks)
      int64_t price_diff =
          current_bbo->ask_price.value - prev_bbo.ask_price.value;
      bool price_ok = (price_diff <= max_price_move);

      // defense_qty_multiplier_ is scaled by kSignalScale, need to divide
      int64_t required_qty =
          (trade->qty.value * defense_qty_multiplier_) / common::kSignalScale;
      bool qty_sufficient = (current_bbo->ask_qty.value >= required_qty);

      if (debug_cfg_.log_defense_check) {
        this->logger_.debug(
            "[Defense] Short | trade_qty:{}, prev_ask:{}/{}, curr_ask:{}/{}, "
            "price_diff:{} (max:{}), result:{}",
            trade->qty.value,
            prev_bbo.ask_price.value,
            prev_bbo.ask_qty.value,
            current_bbo->ask_price.value,
            current_bbo->ask_qty.value,
            price_diff,
            max_price_move,
            price_ok && qty_sufficient);
      }

      return price_ok && qty_sufficient;
    }
  }

  // ========================================
  // Entry Filter Functions
  // ========================================

  /**
   * @brief Check wall quality (spoofing detection)
   * @param wall_tracker Wall tracker to check
   * @param side_name "LONG" or "SHORT" for logging
   * @return true if wall quality is sufficient, false otherwise
   */
  [[nodiscard]] bool check_wall_quality(
      const typename FeatureEngineT::WallTracker& wall_tracker,
      const char* side_name) const {

    constexpr double kMinWallQuality = 0.6;  // 60% minimum threshold
    double wall_quality = wall_tracker.composite_quality();

    if (wall_quality < kMinWallQuality) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[Entry Block] {} | Wall quality too low (spoofing?) | "
            "quality:{:.2f} | stability:{:.2f} | persistence:{:.2f} | "
            "distance:{:.2f}",
            side_name,
            wall_quality,
            wall_tracker.stability_score(),
            wall_tracker.persistence_score(),
            wall_tracker.distance_consistency_score());
      }
      return false;
    }
    return true;
  }

  /**
   * @brief Check OBI direction for LONG entry
   * @param obi Current OBI (scaled by kObiScale)
   * @param z_robust Current Z-score
   * @return true if OBI is valid for LONG entry, false otherwise
   */
  [[nodiscard]] bool check_long_obi_direction(int64_t obi,
      int64_t z_robust) const {
    // OBI must be negative (sell side weakening) but not too negative
    if (obi >= 0) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[Entry Block] Long | OBI not negative | z:{:.2f} | obi:{:.2f}",
            z_robust,
            obi);
      }
      return false;
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
      return false;
    }

    return true;
  }

  /**
   * @brief Check OBI direction for SHORT entry
   * @param obi Current OBI (scaled by kObiScale)
   * @param z_robust Current Z-score
   * @return true if OBI is valid for SHORT entry, false otherwise
   */
  [[nodiscard]] bool check_short_obi_direction(int64_t obi,
      int64_t z_robust) const {
    // OBI must be positive (buy side weakening) but not too positive
    if (obi <= 0) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[Entry Block] Short | OBI not positive | z:{:.2f} | obi:{:.2f}",
            z_robust,
            obi);
      }
      return false;
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
      return false;
    }

    return true;
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

    // 1. Wall quality check (spoofing detection)
    if (!check_wall_quality(bid_wall_tracker_, "LONG"))
      return;

    // 3. Calculate Multi-Factor Signal Score
    int64_t obi = calculate_orderbook_imbalance_int64(order_book);
    int64_t mid_price = (bbo->bid_price.value + bbo->ask_price.value) / 2;
    SignalScore signal =
        calculate_long_signal_score(z_robust, bid_wall_info_, obi, mid_price);
    int64_t composite = signal.composite(entry_cfg_);

    if (composite < entry_cfg_.min_signal_quality) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[Entry Block] LONG | Signal quality too low | "
            "score:{} < {} | z:{} wall:{} obi:{}",
            composite,
            entry_cfg_.min_signal_quality,
            signal.z_score_strength,
            signal.wall_strength,
            signal.obi_strength);
      }
      return;
    }

    // 4. Check Z-score threshold (oversold)
    if (z_robust >= -zscore_mid_threshold_) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[Entry Block] LONG | Z-score too high | z:{} >= -{}",
            z_robust,
            zscore_mid_threshold_);
      }
      return;
    }

    // 5. Wall existence check (CRITICAL)
    if (!bid_wall_info_.is_valid) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info("[Entry Block] Long | No wall | z:{}", z_robust);
      }
      return;
    }

    // 6. OBI direction check
    if (!check_long_obi_direction(obi, z_robust)) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[Entry Block] LONG | OBI direction fail | obi:{} z:{}",
            obi,
            z_robust);
      }
      return;
    }

    // 6. Set position to PENDING state BEFORE sending order
    long_position_.status = PositionStatus::PENDING;
    long_position_.qty = entry_cfg_.position_size_raw;
    long_position_.entry_price = bbo->bid_price.value;
    long_position_.entry_wall_info = bid_wall_info_;
    long_position_.state_time = get_current_time_ns();

    // 8. Execute entry (OrderId stored internally)
    place_entry_order(common::Side::kBuy, bbo->bid_price.value);

    if (debug_cfg_.log_entry_exit) {
      double wall_quality = bid_wall_tracker_.composite_quality();
      this->logger_.info(
          "[Entry Signal] LONG | quality:{} ({}) | wall_quality:{} | "
          "z_robust:{} | "
          "price:{} | wall:{}@{} bps | obi:{} | "
          "components: z={} wall={} obi={}",
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

    // 1. Wall quality check (spoofing detection)
    if (!check_wall_quality(ask_wall_tracker_, "SHORT"))
      return;

    // 3. Calculate Multi-Factor Signal Score
    int64_t obi = calculate_orderbook_imbalance_int64(order_book);
    int64_t mid_price = (bbo->bid_price.value + bbo->ask_price.value) / 2;
    SignalScore signal =
        calculate_short_signal_score(z_robust, ask_wall_info_, obi, mid_price);
    int64_t composite = signal.composite(entry_cfg_);

    if (composite < entry_cfg_.min_signal_quality) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[Entry Block] SHORT | Signal quality too low | "
            "score:{} < {} | z:{} wall:{} obi:{}",
            composite,
            entry_cfg_.min_signal_quality,
            signal.z_score_strength,
            signal.wall_strength,
            signal.obi_strength);
      }
      return;
    }

    // 4. Check if still in overbought territory (but declining)
    // Allow entry if z > threshold * min_ratio (haven't dropped too much)
    if (z_robust * common::kSignalScale <
        zscore_mid_threshold_ * entry_cfg_.short_zscore_min_ratio) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[Entry Block] Short | Already dropped too much | z:{} < {}",
            z_robust,
            (zscore_mid_threshold_ * entry_cfg_.short_zscore_min_ratio) /
                common::kSignalScale);
      }
      return;
    }

    // 5. Wall existence check (CRITICAL)
    if (!ask_wall_info_.is_valid) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info("[Entry Block] Short | No wall | z:{:.2f}",
            z_robust);
      }
      return;
    }

    // 6. OBI direction check
    if (!check_short_obi_direction(obi, z_robust)) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[Entry Block] SHORT | OBI direction fail | obi:{} z:{}",
            obi,
            z_robust);
      }
      return;
    }

    // 6. Set position to PENDING state BEFORE sending order
    short_position_.status = PositionStatus::PENDING;
    short_position_.qty = entry_cfg_.position_size_raw;
    short_position_.entry_price = bbo->ask_price.value;
    short_position_.entry_wall_info = ask_wall_info_;
    short_position_.state_time = get_current_time_ns();

    // 8. Execute entry (OrderId stored internally)
    place_entry_order(common::Side::kSell, bbo->ask_price.value);

    if (debug_cfg_.log_entry_exit) {
      double wall_quality = ask_wall_tracker_.composite_quality();
      this->logger_.info(
          "[Entry Signal] SHORT | quality:{} ({}) | wall_quality:{} | "
          "z_robust:{} | "
          "price:{} | wall:{}@{} bps | obi:{} | "
          "components: z={} wall={} obi={}",
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

    // safety_margin_entry_bps is in basis points, convert to price raw:
    // margin_raw = base_price * margin_bps / kBpsScale
    int64_t margin_raw = (base_price_raw * entry_cfg_.safety_margin_entry_bps) /
                         common::kBpsScale;

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
          entry_cfg_.safety_margin_entry_bps,
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

    // Calculate multi-timeframe z-scores for exit alignment
    int64_t mid_price = (bbo->bid_price.value + bbo->ask_price.value) / 2;
    int64_t z_fast = robust_zscore_fast_->calculate_zscore(mid_price);
    int64_t z_mid = robust_zscore_mid_->calculate_zscore(mid_price);
    int64_t z_slow = robust_zscore_slow_->calculate_zscore(mid_price);
    int64_t current_obi = calculate_orderbook_imbalance_int64(order_book);

    check_long_exit(bbo, mid_price, z_fast, z_mid, z_slow, current_obi);
    check_short_exit(bbo, mid_price, z_fast, z_mid, z_slow, current_obi);
  }

  // ========================================
  // Long position exit (Multi-Factor Scoring + Multi-Timeframe Alignment)
  // ========================================
  void check_long_exit(const BBO* bbo, int64_t mid_price, int64_t z_fast,
      int64_t z_mid, int64_t z_slow, int64_t current_obi) {
    if (long_position_.status != PositionStatus::ACTIVE) {
      return;
    }

    // Skip if exit order already pending
    if (long_position_.pending_order_id.has_value()) {
      return;
    }

    // Multi-timeframe exit alignment: Reversal detection
    // LONG exit: Fast + Mid both turn negative (downtrend starting)
    // Catches both sideways (z→0) and reversal (z<0) scenarios
    bool exit_timeframe_aligned = (z_fast < 0) && (z_mid < 0);

    if (!exit_timeframe_aligned) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[Exit Block] LONG | Timeframes NOT aligned | "
            "z_fast:{} z_mid:{} z_slow:{} (need z_fast<0 && z_mid<0)",
            z_fast,
            z_mid,
            z_slow);
      }
      return;
    }

    // ============================================================
    // TIER 1: 긴급 청산 (Emergency Exit) - 즉시 실행
    // ============================================================

    // 1-1. 벽 소멸 (최우선)
    if (!bid_wall_info_.is_valid) {
      auto order_ids = emergency_exit(common::Side::kSell,
          bbo->bid_price.value,
          "EMERGENCY: Bid wall vanished");
      if (!order_ids.empty()) {
        long_position_.pending_order_id = order_ids[0];
      }
      return;
    }

    // 1-2. 손절 (최우선)
    int64_t pnl_bps =
        ((mid_price - long_position_.entry_price) * common::kBpsScale) /
        long_position_.entry_price;
    if (pnl_bps < -exit_cfg_.max_loss_bps) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[Stop Loss] LONG | entry:{} mid:{} bid:{} ask:{} | "
            "pnl_bps:{} < -{}",
            long_position_.entry_price,
            mid_price,
            bbo->bid_price.value,
            bbo->ask_price.value,
            pnl_bps,
            exit_cfg_.max_loss_bps);
      }
      auto order_ids = emergency_exit(common::Side::kSell,
          bbo->bid_price.value,
          "EMERGENCY: Stop loss");
      if (!order_ids.empty()) {
        long_position_.pending_order_id = order_ids[0];
      }
      return;
    }

    // ============================================================
    // TIER 2: 정상 청산 (Normal Exit) - Multi-Factor Scoring
    // ============================================================

    // 2-1. 모든 청산 신호를 독립적으로 평가 (Mid-only scoring)
    uint64_t hold_time = get_current_time_ns() - long_position_.state_time;
    ExitScore exit_score =
        calculate_long_exit_score(z_mid, current_obi, mid_price, hold_time);

    // 2-2. 복합 점수 계산
    int64_t composite_score = exit_score.composite(exit_cfg_);

    // 2-3. 청산 실행
    if (composite_score >= exit_cfg_.min_exit_quality) {
      const char* reason = "Multi-Factor";

      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[Exit Signal] LONG | reason:{} | "
            "z:{} obi:{} wall:{}/{} time:{}s | "
            "components: z_rev={} obi_rev={} wall_decay={} time_p={}",
            reason,
            z_mid,
            current_obi,
            bid_wall_info_.accumulated_notional,
            long_position_.entry_wall_info.accumulated_notional,
            hold_time / 1'000'000'000,
            exit_score.z_reversion_strength,
            exit_score.obi_reversal_strength,
            exit_score.wall_decay_strength,
            exit_score.time_pressure);
      }

      auto order_ids =
          emergency_exit(common::Side::kSell, bbo->bid_price.value, reason);
      if (!order_ids.empty()) {
        long_position_.pending_order_id = order_ids[0];
      }
    }
  }

  // ========================================
  // Short position exit (Multi-Factor Scoring + Multi-Timeframe Alignment)
  // ========================================
  void check_short_exit(const BBO* bbo, int64_t mid_price, int64_t z_fast,
      int64_t z_mid, int64_t z_slow, int64_t current_obi) {
    if (short_position_.status != PositionStatus::ACTIVE) {
      return;
    }

    // Skip if exit order already pending
    if (short_position_.pending_order_id.has_value()) {
      return;
    }

    // Multi-timeframe exit alignment: Reversal detection
    // SHORT exit: Fast + Mid both turn positive (uptrend starting)
    bool exit_timeframe_aligned = (z_fast > 0) && (z_mid > 0);

    if (!exit_timeframe_aligned) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[Exit Block] SHORT | Timeframes NOT aligned | "
            "z_fast:{} z_mid:{} z_slow:{} (need z_fast>0 && z_mid>0)",
            z_fast,
            z_mid,
            z_slow);
      }
      return;
    }

    // ============================================================
    // TIER 1: 긴급 청산 (Emergency Exit) - 즉시 실행
    // ============================================================

    // 1-1. 벽 소멸 (최우선)
    if (!ask_wall_info_.is_valid) {
      auto order_ids = emergency_exit(common::Side::kBuy,
          bbo->ask_price.value,
          "EMERGENCY: Ask wall vanished");
      if (!order_ids.empty()) {
        short_position_.pending_order_id = order_ids[0];
      }
      return;
    }

    // 1-2. 손절 (최우선)
    int64_t pnl_bps =
        ((short_position_.entry_price - mid_price) * common::kBpsScale) /
        short_position_.entry_price;
    if (pnl_bps < -exit_cfg_.max_loss_bps) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[Stop Loss] SHORT | entry:{} mid:{} bid:{} ask:{} | "
            "pnl_bps:{} < -{}",
            short_position_.entry_price,
            mid_price,
            bbo->bid_price.value,
            bbo->ask_price.value,
            pnl_bps,
            exit_cfg_.max_loss_bps);
      }
      auto order_ids = emergency_exit(common::Side::kBuy,
          bbo->ask_price.value,
          "EMERGENCY: Stop loss");
      if (!order_ids.empty()) {
        short_position_.pending_order_id = order_ids[0];
      }
      return;
    }

    // ============================================================
    // TIER 2: 정상 청산 (Normal Exit) - Multi-Factor Scoring
    // ============================================================

    // 2-1. 모든 청산 신호를 독립적으로 평가 (Mid-only scoring)
    uint64_t hold_time = get_current_time_ns() - short_position_.state_time;
    ExitScore exit_score =
        calculate_short_exit_score(z_mid, current_obi, mid_price, hold_time);

    // 2-2. 복합 점수 계산
    int64_t composite_score = exit_score.composite(exit_cfg_);

    // 2-3. 청산 실행
    if (composite_score >= exit_cfg_.min_exit_quality) {
      const char* reason = "Multi-Factor";

      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[Exit Signal] SHORT | reason:{} | "
            "z:{} obi:{} wall:{}/{} time:{}s | "
            "components: z_rev={} obi_rev={} wall_decay={} time_p={}",
            reason,
            z_mid,
            current_obi,
            ask_wall_info_.accumulated_notional,
            short_position_.entry_wall_info.accumulated_notional,
            hold_time / 1'000'000'000,
            exit_score.z_reversion_strength,
            exit_score.obi_reversal_strength,
            exit_score.wall_decay_strength,
            exit_score.time_pressure);
      }

      auto order_ids =
          emergency_exit(common::Side::kBuy, bbo->ask_price.value, reason);
      if (!order_ids.empty()) {
        short_position_.pending_order_id = order_ids[0];
      }
    }
  }

  // ========================================
  // Emergency exit
  // ========================================
  std::vector<common::OrderId> emergency_exit(common::Side exit_side,
      int64_t market_price_raw, const char* reason) {
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
  bool is_long_reversal_signal() const {
    // Position check: No existing position
    if (long_position_.status != PositionStatus::NONE) {
      return false;
    }

    return true;
  }

  bool is_short_reversal_signal() const {
    // Position check: No existing position
    if (short_position_.status != PositionStatus::NONE) {
      return false;
    }

    return true;
  }

  // ========================================
  // Multi-timeframe Z-score Calculation
  // ========================================

  struct ZScores {
    int64_t z_fast;
    int64_t z_mid;
    int64_t z_slow;
  };

  ZScores calculate_multi_timeframe_zscores(int64_t price) {
    // Calculate z-scores FIRST using historical window
    ZScores result{.z_fast = robust_zscore_fast_->calculate_zscore(price),
        .z_mid = robust_zscore_mid_->calculate_zscore(price),
        .z_slow = robust_zscore_slow_->calculate_zscore(price)};

    // THEN add new price to window for next calculation
    robust_zscore_fast_->on_price(price);
    robust_zscore_mid_->on_price(price);
    robust_zscore_slow_->on_price(price);

    return result;
  }

  void handle_adverse_selection(uint64_t now, int64_t price) {
    adverse_selection_tracker_.on_price_update(now,
        price,
        adverse_selection_cfg_);

    if (adverse_selection_tracker_.is_being_picked_off(
            adverse_selection_cfg_)) {
      const_cast<EntryConfig&>(entry_cfg_).safety_margin_entry_bps =
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
            entry_cfg_.safety_margin_entry_bps);
      }
    } else {
      const_cast<EntryConfig&>(entry_cfg_).safety_margin_entry_bps =
          original_safety_margin_bps_;
    }
  }

  bool check_long_momentum_weakening(int64_t z_slow, int64_t z_mid,
      int64_t z_fast) const {
    // Must be in positive territory (overbought)
    if (z_slow <= 0 || z_mid <= 0)
      return false;

    return (z_fast < zscore_fast_threshold_) &&
           (z_mid < zscore_mid_threshold_) && (z_slow > zscore_slow_threshold_);
  }

  bool check_short_momentum_weakening(int64_t z_slow, int64_t z_mid,
      int64_t z_fast) const {
    // Must be in negative territory (oversold)
    if (z_mid >= 0 || z_fast >= 0)
      return false;

    return (z_fast > -zscore_fast_threshold_) &&
           (z_mid > -zscore_mid_threshold_) &&
           (z_slow < -zscore_slow_threshold_);
  }

  void try_long_entry(const MarketData* market_data,
      MarketOrderBookT* order_book, const BBO* current_bbo,
      const ZScores& zscores) {
    if (!is_short_reversal_signal()) {
      return;
    }

    if (!bid_wall_info_.is_valid) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info("[Entry Skip LONG] No bid wall");
      }
      return;
    }

    bool defense_ok = validate_defense_realtime(market_data,
        prev_bbo_,
        current_bbo,
        common::Side::kBuy);
    if (!defense_ok) {
      if (debug_cfg_.log_entry_exit) {
        int64_t price_diff =
            prev_bbo_.bid_price.value - current_bbo->bid_price.value;
        int64_t required_qty =
            (market_data->qty.value * defense_qty_multiplier_) /
            common::kSignalScale;
        bool qty_sufficient = (current_bbo->bid_qty.value >= required_qty);

        this->logger_.info(
            "[Entry Skip LONG] Defense fail | price_diff:{} ({} -> {}) | "
            "qty_sufficient:{} ({} vs {} required) | trade_qty:{}",
            price_diff,
            prev_bbo_.bid_price.value,
            current_bbo->bid_price.value,
            qty_sufficient,
            current_bbo->bid_qty.value,
            required_qty,
            market_data->qty.value);
      }
      return;
    }

    check_long_entry(market_data, order_book, current_bbo, zscores.z_mid);
  }

  void try_short_entry(const MarketData* market_data,
      MarketOrderBookT* order_book, const BBO* current_bbo,
      const ZScores& zscores) {
    if (!is_long_reversal_signal()) {
      return;
    }

    if (!ask_wall_info_.is_valid) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info("[Entry Skip SHORT] No ask wall");
      }
      return;
    }

    bool defense_ok = validate_defense_realtime(market_data,
        prev_bbo_,
        current_bbo,
        common::Side::kSell);
    if (!defense_ok) {
      if (debug_cfg_.log_entry_exit) {
        int64_t price_diff =
            current_bbo->ask_price.value - prev_bbo_.ask_price.value;
        int64_t required_qty =
            (market_data->qty.value * defense_qty_multiplier_) /
            common::kSignalScale;
        bool qty_sufficient = (current_bbo->ask_qty.value >= required_qty);

        this->logger_.info(
            "[Entry Skip SHORT] Defense fail | price_diff:{} ({} -> {}) | "
            "qty_sufficient:{} ({} vs {} required) | trade_qty:{}",
            price_diff,
            prev_bbo_.ask_price.value,
            current_bbo->ask_price.value,
            qty_sufficient,
            current_bbo->ask_qty.value,
            required_qty,
            market_data->qty.value);
      }
      return;
    }

    check_short_entry(market_data, order_book, current_bbo, zscores.z_mid);
  }

  // ========================================
  // Multi-Factor Signal Scoring
  // ========================================

  /**
   * @brief Calculate long entry signal score (Mid-only scoring)
   * @param z Z-score value from MID timeframe (scaled by kZScoreScale)
   *          Fast/Slow are used for alignment check only, Mid determines strength
   * @param wall Wall information
   * @param obi Orderbook imbalance (scaled by kObiScale)
   * @param mid_price Mid price in raw scale (for wall target calculation)
   * @return SignalScore with all components normalized to [0, kSignalScale]
   */
  SignalScore calculate_long_signal_score(int64_t z,
      const FeatureEngineT::WallInfo& wall, int64_t obi,
      int64_t mid_price) const {
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
    // Convert min_quantity to notional first: min_qty * mid_price / kQtyScale
    // Then apply multiplier: notional * multiplier / kSignalScale
    int64_t min_notional =
        (dynamic_threshold_->get_min_quantity() * mid_price) /
        common::FixedPointConfig::kQtyScale;
    int64_t wall_target =
        (min_notional * entry_cfg_.wall_norm_multiplier) / common::kSignalScale;
    if (wall_target > 0) {
      int64_t wall_normalized =
          (wall.accumulated_notional * common::kSignalScale) / wall_target;
      score.wall_strength =
          std::clamp(wall_normalized, int64_t{0}, common::kSignalScale);
    }

    // === 3. OBI strength: normalize to [0, kSignalScale] ===
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
   * @brief Calculate short entry signal score (Mid-only scoring)
   * @param z Z-score value from MID timeframe (scaled by kZScoreScale)
   *          Fast/Slow are used for alignment check only, Mid determines strength
   * @param wall Wall information
   * @param obi Orderbook imbalance
   * @param mid_price Mid price in raw scale (for wall target calculation)
   * @return SignalScore with all components normalized to [0, kSignalScale]
   */
  SignalScore calculate_short_signal_score(int64_t z,
      const FeatureEngineT::WallInfo& wall, int64_t obi,
      int64_t mid_price) const {
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
    // Convert min_quantity to notional first: min_qty * mid_price / kQtyScale
    // Then apply multiplier: notional * multiplier / kSignalScale
    int64_t min_notional =
        (dynamic_threshold_->get_min_quantity() * mid_price) /
        common::FixedPointConfig::kQtyScale;
    int64_t wall_target =
        (min_notional * entry_cfg_.wall_norm_multiplier) / common::kSignalScale;
    if (wall_target > 0) {
      int64_t wall_normalized =
          (wall.accumulated_notional * common::kSignalScale) / wall_target;
      score.wall_strength =
          std::clamp(wall_normalized, int64_t{0}, common::kSignalScale);
    }

    // === 3. OBI strength: normalize to [0, kSignalScale] ===
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
  // Multi-Factor Exit Scoring (진입 로직과 동일한 패턴)
  // ========================================

  /**
   * @brief Z-score reversion strength for exit
   * @param current_z Current Z-score (signed)
   * @param exit_threshold Exit threshold (positive value)
   * @param is_long true for LONG, false for SHORT
   * @return Strength [0, kSignalScale]
   */
  [[nodiscard]] int64_t calculate_z_reversion_strength(int64_t current_z,
      int64_t exit_threshold, bool is_long) const {

    // LONG: z moves from -2.0 → 0 (reversion)
    // SHORT: z moves from +2.0 → 0 (reversion)
    int64_t z_from_threshold = is_long ? (current_z - (-exit_threshold))
                                       : ((-current_z) - (-exit_threshold));

    if (z_from_threshold <= 0)
      return 0;

    int64_t z_max_range = exit_threshold;
    if (z_max_range <= 0)
      return 0;

    return std::clamp((z_from_threshold * common::kSignalScale) / z_max_range,
        int64_t{0},
        common::kSignalScale);
  }

  /**
   * @brief OBI reversal strength for exit
   * @param current_obi Current OBI (signed)
   * @param exit_threshold Exit threshold (positive value)
   * @param is_long true for LONG, false for SHORT
   * @return Strength [0, kSignalScale]
   */
  [[nodiscard]] int64_t calculate_obi_reversal_strength(int64_t current_obi,
      int64_t exit_threshold, bool is_long) const {

    // LONG: OBI < -exit_threshold (매도세 강화)
    // SHORT: OBI > +exit_threshold (매수세 강화)
    bool reversal_condition = is_long ? (current_obi < -exit_threshold)
                                      : (current_obi > exit_threshold);

    if (!reversal_condition)
      return 0;

    int64_t obi_excess = is_long ? (std::abs(current_obi) - exit_threshold)
                                 : (current_obi - exit_threshold);

    if (obi_excess <= 0)
      return 0;

    int64_t obi_max_range = common::kObiScale - exit_threshold;
    if (obi_max_range <= 0)
      return 0;

    return std::clamp((obi_excess * common::kSignalScale) / obi_max_range,
        int64_t{0},
        common::kSignalScale);
  }

  /**
   * @brief Wall decay strength for exit
   * @param current_wall_info Current wall info
   * @param entry_wall_info Entry wall info
   * @return Strength [0, kSignalScale]
   */
  [[nodiscard]] int64_t calculate_wall_decay_strength(
      const typename FeatureEngineT::WallInfo& current_wall_info,
      const typename FeatureEngineT::WallInfo& entry_wall_info) const {

    if (entry_wall_info.accumulated_notional <= 0)
      return 0;

    int64_t wall_ratio =
        (current_wall_info.accumulated_notional * common::kSignalScale) /
        entry_wall_info.accumulated_notional;

    int64_t strength = 0;

    // 1. Quantity decay component
    if (wall_ratio < exit_cfg_.wall_amount_decay_ratio) {
      int64_t decay_delta = exit_cfg_.wall_amount_decay_ratio - wall_ratio;
      int64_t decay_range = exit_cfg_.wall_amount_decay_ratio;

      if (decay_range > 0) {
        strength =
            std::clamp((decay_delta * common::kSignalScale) / decay_range,
                int64_t{0},
                common::kSignalScale);
      }
    }

    // 2. Distance expansion component (boost if wall moved away)
    if (entry_wall_info.distance_bps > 0 &&
        current_wall_info.distance_bps * common::kSignalScale >
            entry_wall_info.distance_bps *
                exit_cfg_.wall_distance_expand_ratio) {

      int64_t distance_ratio =
          (current_wall_info.distance_bps * common::kSignalScale) /
          entry_wall_info.distance_bps;
      int64_t distance_excess =
          distance_ratio - exit_cfg_.wall_distance_expand_ratio;
      int64_t distance_contribution =
          std::min(distance_excess / 2, common::kSignalScale / 2);

      strength =
          std::min(strength + distance_contribution, common::kSignalScale);
    }

    return strength;
  }

  /**
   * @brief Time pressure strength for exit
   * @param hold_time_ns Position holding time (nanoseconds)
   * @return Strength [0, kSignalScale]
   */
  [[nodiscard]] int64_t calculate_time_pressure(uint64_t hold_time_ns) const {
    if (hold_time_ns == 0)
      return 0;

    uint64_t soft_time = (exit_cfg_.max_hold_time_ns *
                             static_cast<uint64_t>(exit_cfg_.soft_time_ratio)) /
                         static_cast<uint64_t>(common::kSignalScale);

    int64_t pressure = 0;

    if (hold_time_ns < soft_time) {
      // Gentle slope: 0 ~ soft_time_ratio
      pressure = (hold_time_ns * exit_cfg_.soft_time_ratio) / soft_time;
    } else {
      // Steep slope: soft_time_ratio ~ kSignalScale
      uint64_t excess_time = hold_time_ns - soft_time;
      uint64_t remaining_time = exit_cfg_.max_hold_time_ns - soft_time;

      if (remaining_time > 0) {
        int64_t excess_pressure =
            (excess_time * (common::kSignalScale - exit_cfg_.soft_time_ratio)) /
            remaining_time;
        pressure = exit_cfg_.soft_time_ratio + excess_pressure;
      }
    }

    return std::clamp(pressure, int64_t{0}, common::kSignalScale);
  }

  /**
   * @brief Calculate long exit signal score
   * @param current_z Current Z-score
   * @param current_obi Current OBI
   * @param mid_price Current mid price
   * @param hold_time_ns Position holding time (nanoseconds)
   * @return ExitScore with all components
   */
  ExitScore calculate_long_exit_score(int64_t current_z, int64_t current_obi,
      int64_t /* mid_price */, uint64_t hold_time_ns) const {

    ExitScore score;

    score.z_reversion_strength = calculate_z_reversion_strength(current_z,
        exit_cfg_.zscore_exit_threshold,
        true);

    score.obi_reversal_strength = calculate_obi_reversal_strength(current_obi,
        exit_cfg_.obi_exit_threshold,
        true);

    score.wall_decay_strength = calculate_wall_decay_strength(bid_wall_info_,
        long_position_.entry_wall_info);

    score.time_pressure = calculate_time_pressure(hold_time_ns);

    return score;
  }

  /**
   * @brief Calculate short exit signal score
   * (로직은 LONG과 대칭, Z-score와 OBI 부호만 반대)
   */
  ExitScore calculate_short_exit_score(int64_t current_z, int64_t current_obi,
      int64_t /* mid_price */, uint64_t hold_time_ns) const {

    ExitScore score;

    score.z_reversion_strength = calculate_z_reversion_strength(current_z,
        exit_cfg_.zscore_exit_threshold,
        false);

    score.obi_reversal_strength = calculate_obi_reversal_strength(current_obi,
        exit_cfg_.obi_exit_threshold,
        false);

    score.wall_decay_strength = calculate_wall_decay_strength(ask_wall_info_,
        short_position_.entry_wall_info);

    score.time_pressure = calculate_time_pressure(hold_time_ns);

    return score;
  }

  // ========================================
  // Member variables (int64_t version)
  // ========================================
  // Config parameters (grouped)
  const int64_t defense_qty_multiplier_;  // scaled by kSignalScale
  const int64_t zscore_mid_threshold_;    // scaled by kZScoreScale
  const int64_t zscore_fast_threshold_;   // scaled by kZScoreScale
  const int64_t zscore_slow_threshold_;   // scaled by kZScoreScale

  const WallDetectionConfig wall_cfg_;
  const EntryConfig entry_cfg_;
  const ExitConfig exit_cfg_;
  const DebugLoggingConfig debug_cfg_;
  const MeanReversionConfig mean_reversion_cfg_;
  const NormalizationConfig normalization_cfg_;
  const AdverseSelectionConfig adverse_selection_cfg_;

  // Z-score config (kept separate for module initialization)
  const int zscore_window_size_;
  const int zscore_min_samples_;
  const int64_t zscore_min_mad_threshold_raw_;  // scaled by kPriceScale

  // Multi-timeframe Z-score config
  const int zscore_fast_window_;
  const int zscore_fast_min_samples_;
  const int zscore_mid_window_;
  const int zscore_mid_min_samples_;
  const int zscore_slow_window_;
  const int zscore_slow_min_samples_;

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

  // Throttling timestamp for orderbook updates
  uint64_t last_orderbook_check_time_{0};
};

}  // namespace trading

#endif  // MEAN_REVERSION_MAKER_H
