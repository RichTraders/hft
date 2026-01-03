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

// === Strategy Configuration Structures ===
struct WallDetectionConfig {
  double max_distance_pct{0.0015};
  int max_levels{100};
};

struct EntryConfig {
  double obi_threshold{0.25};
  int obi_levels{5};
  double position_size{0.01};
  double safety_margin{0.00005};
  double min_spread_filter{0.0004};

  // Multi-Factor Scoring parameters
  double min_signal_quality{0.65};   // Minimum composite score for entry
  double zscore_weight{0.35};        // Z-score component weight
  double wall_weight{0.30};          // Wall strength weight
  double volume_weight{0.20};        // Volume reversal weight
  double obi_weight{0.15};           // OBI alignment weight
  double zscore_norm_min{2.0};       // Z-score normalization min
  double zscore_norm_max{3.0};       // Z-score normalization max
  double wall_norm_multiplier{2.0};  // Wall normalization multiplier
  double obi_norm_min{0.05};         // OBI normalization min
  double obi_norm_max{0.25};         // OBI normalization max
  int volume_score_lookback{5};      // Volume analysis window
};

struct ExitConfig {
  bool enabled{true};  // Enable/disable position exit monitoring
  double wall_amount_decay_ratio{0.5};
  double wall_distance_expand_ratio{1.2};
  double max_loss_pct{0.002};
  uint64_t max_hold_time_ns{5'000'000'000};  // 5 seconds default (HFT)
  double max_price_deviation_pct{
      0.002};  // 0.2% max deviation from current price
  bool cancel_on_wall_decay{true};

  // Active exit conditions (profit-taking)
  double zscore_exit_threshold{0.5};  // Z-score mean reversion threshold
  double obi_exit_threshold{0.3};     // OBI reversal threshold
  bool reversal_momentum_exit{true};  // Enable volume reversal exit
  int exit_lookback_ticks{10};        // Exit momentum lookback
  int exit_min_directional_ticks{7};  // 70% directional ticks required
  double exit_min_volume_ratio{1.5};  // 1.5x volume ratio for exit
};

struct TrendFilterConfig {
  int lookback_ticks{5};
  int consecutive_threshold{4};
  double volume_multiplier{1.5};
};

struct ReversalMomentumConfig {
  bool enabled{true};
  int lookback_ticks{5};
  int min_directional_ticks{3};
  double min_volume_ratio{1.2};
};

struct DebugLoggingConfig {
  bool log_wall_detection{false};
  bool log_defense_check{false};
  bool log_entry_exit{false};
};

struct MeanReversionConfig {
  // Legacy parameters (backwards compatibility)
  double oversold_start_threshold{1.5};    // Deprecated
  double overbought_start_threshold{1.5};  // Deprecated
  double min_reversal_bounce{0.2};         // Minimum bounce from extreme
  double neutral_zone_threshold{1.0};      // Reset threshold

  // 5-State threshold multipliers (relative to adaptive_threshold)
  double building_multiplier{1.0};       // NEUTRAL → BUILDING
  double deep_multiplier{1.2};           // BUILDING → DEEP
  double reversal_weak_multiplier{0.8};  // DEEP → REVERSAL_WEAK
  double reversal_strong_multiplier{
      0.6};  // WEAK → STRONG (not used in current logic)

  // False reversal detection
  double false_reversal_ratio{
      0.5};  // Ratio of min_reversal_bounce for false reversal
};

// ==========================================
// Multi-Factor Signal Scoring
// ==========================================
/**
 * @brief Entry signal quality score (0-1 range for each component)
 *
 * Replaces boolean entry signals with scored signals to capture
 * signal strength and filter low-quality setups.
 *
 * Example:
 * - Z-score -2.1 → z_score_strength = 0.1
 * - Z-score -3.0 → z_score_strength = 1.0
 * - composite() = weighted average of all components
 */
struct SignalScore {
  double z_score_strength{0.0};  // 0-1: Z-score magnitude normalized
  double wall_strength{0.0};     // 0-1: Wall size vs threshold
  double volume_strength{0.0};   // 0-1: Directional volume momentum
  double obi_strength{0.0};      // 0-1: Orderbook imbalance alignment

  /**
   * @brief Calculate composite score (weighted average)
   * @param weights Entry config with component weights
   * @return Composite score (0-1)
   */
  double composite(const EntryConfig& cfg) const {
    return cfg.zscore_weight * z_score_strength +
           cfg.wall_weight * wall_strength +
           cfg.volume_weight * volume_strength + cfg.obi_weight * obi_strength;
  }

  /**
   * @brief Get signal quality classification
   * @param cfg Entry config with min_signal_quality threshold
   * @return Quality level (EXCELLENT/GOOD/MARGINAL/POOR)
   */
  enum class Quality { EXCELLENT, GOOD, MARGINAL, POOR };

  Quality get_quality(const EntryConfig& cfg) const {
    double score = composite(cfg);
    if (score > 0.8)
      return Quality::EXCELLENT;
    if (score >= cfg.min_signal_quality)
      return Quality::GOOD;
    if (score > 0.5)
      return Quality::MARGINAL;
    return Quality::POOR;
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

  // === Position state structure ===
  struct PositionState {
    double qty{0.0};
    double entry_price{0.0};
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
        // === Position mode ===
        allow_simultaneous_positions_(
            INI_CONFIG.get_int("strategy", "allow_simultaneous_positions", 0)),

        // === Defense ===
        defense_qty_multiplier_(
            INI_CONFIG.get_double("wall_defense", "qty_multiplier", 2.0)),

        // === Z-score threshold ===
        zscore_entry_threshold_(
            INI_CONFIG.get_double("robust_zscore", "entry_threshold", 2.5)),

        // === Config structures ===
        wall_cfg_{
            INI_CONFIG.get_double("wall_detection", "max_distance_pct", 0.0015),
            INI_CONFIG.get_int("wall_detection", "max_levels", 100)},

        entry_cfg_{INI_CONFIG.get_double("entry", "obi_threshold", 0.25),
            INI_CONFIG.get_int("entry", "obi_levels", 5),
            INI_CONFIG.get_double("entry", "position_size", 0.01),
            INI_CONFIG.get_double("entry", "safety_margin", 0.00005),
            INI_CONFIG.get_double("entry", "min_spread_filter", 0.0004),
            // Multi-Factor Scoring
            INI_CONFIG.get_double("entry", "min_signal_quality", 0.65),
            INI_CONFIG.get_double("entry", "zscore_weight", 0.35),
            INI_CONFIG.get_double("entry", "wall_weight", 0.30),
            INI_CONFIG.get_double("entry", "volume_weight", 0.20),
            INI_CONFIG.get_double("entry", "obi_weight", 0.15),
            INI_CONFIG.get_double("entry", "zscore_norm_min", 2.0),
            INI_CONFIG.get_double("entry", "zscore_norm_max", 3.0),
            INI_CONFIG.get_double("entry", "wall_norm_multiplier", 2.0),
            INI_CONFIG.get_double("entry", "obi_norm_min", 0.05),
            INI_CONFIG.get_double("entry", "obi_norm_max", 0.25),
            INI_CONFIG.get_int("entry", "volume_score_lookback", 5)},

        exit_cfg_{INI_CONFIG.get("exit", "enabled", "true") == "true",
            INI_CONFIG.get_double("exit", "wall_amount_decay_ratio", 0.5),
            INI_CONFIG.get_double("exit", "wall_distance_expand_ratio", 1.2),
            INI_CONFIG.get_double("exit", "max_loss_pct", 0.002),
            static_cast<uint64_t>(
                INI_CONFIG.get_double("exit", "max_hold_time_sec", 5.0) *
                1'000'000'000),
            INI_CONFIG.get_double("exit", "max_price_deviation_pct", 0.002),
            INI_CONFIG.get("exit", "cancel_on_wall_decay", "true") == "true",
            INI_CONFIG.get_double("exit", "zscore_exit_threshold", 0.5),
            INI_CONFIG.get_double("exit", "obi_exit_threshold", 0.3),
            INI_CONFIG.get("exit", "reversal_momentum_exit", "true") == "true",
            INI_CONFIG.get_int("exit", "exit_lookback_ticks", 10),
            INI_CONFIG.get_int("exit", "exit_min_directional_ticks", 7),
            INI_CONFIG.get_double("exit", "exit_min_volume_ratio", 1.5)},

        trend_cfg_{INI_CONFIG.get_int("trend_filter", "lookback_ticks", 5),
            INI_CONFIG.get_int("trend_filter", "consecutive_threshold", 4),
            INI_CONFIG.get_double("trend_filter", "volume_multiplier", 1.5)},

        reversal_cfg_{
            INI_CONFIG.get("reversal_momentum", "enabled", "true") == "true",
            INI_CONFIG.get_int("reversal_momentum", "lookback_ticks", 5),
            INI_CONFIG.get_int("reversal_momentum", "min_directional_ticks", 3),
            INI_CONFIG.get_double("reversal_momentum", "min_volume_ratio",
                1.2)},

        debug_cfg_{
            INI_CONFIG.get("debug", "log_wall_detection", "false") == "true",
            INI_CONFIG.get("debug", "log_defense_check", "false") == "true",
            INI_CONFIG.get("debug", "log_entry_exit", "false") == "true"},

        mean_reversion_cfg_{INI_CONFIG.get_double("mean_reversion",
                                "oversold_start_threshold", 1.5),
            INI_CONFIG.get_double("mean_reversion",
                "overbought_start_threshold", 1.5),
            INI_CONFIG.get_double("mean_reversion", "min_reversal_bounce", 0.2),
            INI_CONFIG.get_double("mean_reversion", "neutral_zone_threshold",
                1.0),
            INI_CONFIG.get_double("mean_reversion", "building_multiplier", 1.0),
            INI_CONFIG.get_double("mean_reversion", "deep_multiplier", 1.2),
            INI_CONFIG.get_double("mean_reversion", "reversal_weak_multiplier",
                0.8),
            INI_CONFIG.get_double("mean_reversion",
                "reversal_strong_multiplier", 0.6),
            INI_CONFIG.get_double("mean_reversion", "false_reversal_ratio",
                0.5)},

        // === Z-score config ===
        zscore_window_size_(
            INI_CONFIG.get_int("robust_zscore", "window_size", 30)),
        zscore_min_samples_(
            INI_CONFIG.get_int("robust_zscore", "min_samples", 20)),
        zscore_min_mad_threshold_(
            INI_CONFIG.get_double("robust_zscore", "min_mad_threshold", 5.0)),

        // === Multi-timeframe Z-score config ===
        zscore_fast_window_(
            INI_CONFIG.get_int("robust_zscore_fast", "window_size", 10)),
        zscore_fast_min_samples_(
            INI_CONFIG.get_int("robust_zscore_fast", "min_samples", 8)),
        zscore_slow_window_(
            INI_CONFIG.get_int("robust_zscore_slow", "window_size", 100)),
        zscore_slow_min_samples_(
            INI_CONFIG.get_int("robust_zscore_slow", "min_samples", 60)),
        zscore_slow_threshold_(INI_CONFIG.get_double("robust_zscore_slow",
            "entry_threshold", 1.5)),

        // === OBI calculation buffers ===
        bid_qty_(entry_cfg_.obi_levels),
        ask_qty_(entry_cfg_.obi_levels),

        // === Wall detection buffers ===
        wall_level_qty_(wall_cfg_.max_levels),
        wall_level_idx_(wall_cfg_.max_levels),

        current_wall_threshold_(0.0),

        // === Dynamic threshold module ===
        dynamic_threshold_(std::make_unique<DynamicWallThreshold>(
            VolumeThresholdConfig{
                INI_CONFIG.get_double("wall_defense", "volume_ema_alpha", 0.03),
                INI_CONFIG.get_double("wall_defense", "volume_multiplier", 4.0),
                INI_CONFIG.get_int("wall_defense", "volume_min_samples", 20)},
            OrderbookThresholdConfig{
                INI_CONFIG.get_int("wall_defense", "orderbook_top_levels", 20),
                INI_CONFIG.get_double("wall_defense", "orderbook_multiplier",
                    3.0),
                INI_CONFIG.get_double("wall_defense", "orderbook_percentile",
                    80)},
            HybridThresholdConfig{
                INI_CONFIG.get_double("wall_defense", "volume_weight", 0.7),
                INI_CONFIG.get_double("wall_defense", "orderbook_weight", 0.3),
                INI_CONFIG.get_double("wall_defense", "min_quantity", 50.0)})),

        // === Robust Z-score modules (multi-timeframe) ===
        robust_zscore_fast_(std::make_unique<RobustZScore>(RobustZScoreConfig{
            zscore_fast_window_,
            zscore_fast_min_samples_,
            zscore_min_mad_threshold_,
            INI_CONFIG.get_int("robust_zscore_fast", "baseline_window", 100),
            INI_CONFIG.get_double("robust_zscore_fast", "min_vol_scalar", 0.7),
            INI_CONFIG.get_double("robust_zscore_fast", "max_vol_scalar", 1.3),
            INI_CONFIG.get_double("robust_zscore_fast", "vol_ratio_low", 0.5),
            INI_CONFIG.get_double("robust_zscore_fast", "vol_ratio_high", 2.0),
            INI_CONFIG.get_int("robust_zscore_fast", "baseline_min_history",
                30)})),

        robust_zscore_mid_(std::make_unique<RobustZScore>(RobustZScoreConfig{
            zscore_window_size_,
            zscore_min_samples_,
            zscore_min_mad_threshold_,
            INI_CONFIG.get_int("robust_zscore", "baseline_window", 100),
            INI_CONFIG.get_double("robust_zscore", "min_vol_scalar", 0.7),
            INI_CONFIG.get_double("robust_zscore", "max_vol_scalar", 1.3),
            INI_CONFIG.get_double("robust_zscore", "vol_ratio_low", 0.5),
            INI_CONFIG.get_double("robust_zscore", "vol_ratio_high", 2.0),
            INI_CONFIG.get_int("robust_zscore", "baseline_min_history", 30)})),

        robust_zscore_slow_(std::make_unique<RobustZScore>(RobustZScoreConfig{
            zscore_slow_window_,
            zscore_slow_min_samples_,
            zscore_min_mad_threshold_,
            INI_CONFIG.get_int("robust_zscore_slow", "baseline_window", 100),
            INI_CONFIG.get_double("robust_zscore_slow", "min_vol_scalar", 0.7),
            INI_CONFIG.get_double("robust_zscore_slow", "max_vol_scalar", 1.3),
            INI_CONFIG.get_double("robust_zscore_slow", "vol_ratio_low", 0.5),
            INI_CONFIG.get_double("robust_zscore_slow", "vol_ratio_high", 2.0),
            INI_CONFIG.get_int("robust_zscore_slow", "baseline_min_history",
                30)})) {
    this->logger_.info(
        "[MeanReversionMaker] Initialized | min_quantity:{:.2f} BTC | "
        "simultaneous:{}",
        dynamic_threshold_->get_min_quantity(),
        allow_simultaneous_positions_);
  }

  // ========================================
  // 100ms interval: Orderbook update
  // ========================================
  void on_orderbook_updated(const common::TickerId& ticker, common::Price,
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
    current_wall_threshold_ =
        dynamic_threshold_->calculate(order_book, current_time);

    // === 3. Detect walls (bidirectional) ===
    const int min_price_int = order_book->config().min_price_int;
    // Detect walls (for reference, not for gating entry)
    bid_wall_info_ = this->feature_engine_->detect_wall(order_book,
        common::Side::kBuy,
        wall_cfg_.max_levels,
        current_wall_threshold_,
        wall_cfg_.max_distance_pct,
        min_price_int,
        wall_level_qty_,
        wall_level_idx_);

    ask_wall_info_ = this->feature_engine_->detect_wall(order_book,
        common::Side::kSell,
        wall_cfg_.max_levels,
        current_wall_threshold_,
        wall_cfg_.max_distance_pct,
        min_price_int,
        wall_level_qty_,
        wall_level_idx_);

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

    // Calculate Z-scores for all timeframes
    double z_fast =
        robust_zscore_fast_->calculate_zscore(market_data->price.value);
    double z_mid =
        robust_zscore_mid_->calculate_zscore(market_data->price.value);
    double z_slow =
        robust_zscore_slow_->calculate_zscore(market_data->price.value);

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
                "[Entry Skip] Reversal aligned but no wall | z_mid:{:.2f} "
                "z_slow:{:.2f}",
                z_mid,
                z_slow);
          }
        }
      } else {
        if (debug_cfg_.log_entry_exit) {
          this->logger_.info(
              "[Entry Skip] Reversal detected but timeframes NOT aligned | "
              "z_fast:{:.2f} z_mid:{:.2f} z_slow:{:.2f}",
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
                "[Entry Skip] Reversal aligned but no wall | z_mid:{:.2f} "
                "z_slow:{:.2f}",
                z_mid,
                z_slow);
          }
        }
      } else {
        if (debug_cfg_.log_entry_exit) {
          this->logger_.info(
              "[Entry Skip] Reversal detected but timeframes NOT aligned | "
              "z_fast:{:.2f} z_mid:{:.2f} z_slow:{:.2f}",
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

            if (debug_cfg_.log_entry_exit) {
              this->logger_.info(
                  "[Entry Filled] LONG | qty:{} | price:{} | "
                  "wall:${:.0f}@{:.4f}%",
                  report->last_qty.value,
                  report->avg_price.value,
                  long_position_.entry_wall_info.accumulated_amount,
                  long_position_.entry_wall_info.distance_pct * 100);
            }
          } else {
            // LATE FILL DETECTED!
            const double actual_position = pos_info->long_position_;

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
                 pos_info->long_position_ > 0.0) {
          const double actual_position = pos_info->long_position_;

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

            if (debug_cfg_.log_entry_exit) {
              this->logger_.info(
                  "[Entry Filled] SHORT | qty:{} | price:{} | "
                  "wall:${:.0f}@{:.4f}%",
                  report->last_qty.value,
                  report->avg_price.value,
                  short_position_.entry_wall_info.accumulated_amount,
                  short_position_.entry_wall_info.distance_pct * 100);
            }
          } else {
            // LATE FILL DETECTED!
            const double actual_position = pos_info->short_position_;

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
                 pos_info->short_position_ > 0.0) {
          const double actual_position = pos_info->short_position_;

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
        pos_info->long_position_ == 0.0) {
      long_position_.status = PositionStatus::NONE;
      long_position_.pending_order_id.reset();  // Clear exit order ID
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info("[Exit Complete] Long closed | PnL: {:.2f}",
            pos_info->long_real_pnl_);
      }
    }

    if (short_position_.status == PositionStatus::ACTIVE &&
        pos_info->short_position_ == 0.0) {
      short_position_.status = PositionStatus::NONE;
      short_position_.pending_order_id.reset();  // Clear exit order ID
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info("[Exit Complete] Short closed | PnL: {:.2f}",
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
  // OBI calculation
  // ========================================
  double calculate_orderbook_imbalance(const MarketOrderBookT* order_book) {
    // 1. Extract quantities from orderbook
    (void)order_book->peek_qty(true, entry_cfg_.obi_levels, bid_qty_, {});
    (void)order_book->peek_qty(false, entry_cfg_.obi_levels, ask_qty_, {});

    // 2. Use FeatureEngine's optimized OBI calculation (loop unrolling)
    return FeatureEngineT::orderbook_imbalance_from_levels(bid_qty_, ask_qty_);
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
        directional_volume += trades[i].qty;
      } else {
        opposite_volume += trades[i].qty;
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
        opposite_volume += trades[i].qty;
      } else {
        current_volume += trades[i].qty;
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
      const BBO* bbo, double z_robust) {
    // Z-score is passed as parameter to avoid redundant calculation

    if (debug_cfg_.log_entry_exit) {
      this->logger_.info(
          "[RobustZ] price:{} | median:{:.4f} | MAD:{:.4f} | z:{:.4f}",
          trade->price.value,
          robust_zscore_mid_->get_median(),
          robust_zscore_mid_->get_mad(),
          z_robust);
    }

    // 1. Calculate Multi-Factor Signal Score
    double obi = calculate_orderbook_imbalance(order_book);
    SignalScore signal =
        calculate_long_signal_score(z_robust, bid_wall_info_, obi);
    double composite = signal.composite(entry_cfg_);

    // Check signal quality threshold
    if (composite < entry_cfg_.min_signal_quality) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[Entry Block] LONG | Signal quality too low | "
            "score:{:.2f} < {:.2f} | z:{:.2f} wall:{:.2f} vol:{:.2f} "
            "obi:{:.2f}",
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
        this->logger_.info("[Entry Block] Long | No wall | z:{:.2f}", z_robust);
      }
      return;
    }

    // 4. Trend acceleration filter (SAFETY)
    if (this->feature_engine_->is_trend_accelerating(common::Side::kSell,
            trend_cfg_.lookback_ticks,
            trend_cfg_.consecutive_threshold,
            trend_cfg_.volume_multiplier)) {
      if (debug_cfg_.log_entry_exit) {
        const auto* trades = this->feature_engine_->get_recent_trades();
        const size_t trade_count =
            this->feature_engine_->get_trade_history_size();
        int sell_count = 0;
        size_t count = std::min(trade_count,
            static_cast<size_t>(trend_cfg_.lookback_ticks));
        for (size_t i = 0; i < count; ++i) {
          size_t idx = trade_count - count + i;
          if (trades[idx].side == common::Side::kSell)
            sell_count++;
        }
        this->logger_.info(
            "[Entry Block] Long | Trend accelerating | z:{:.2f} | sells:{}/{}",
            z_robust,
            sell_count,
            count);
      }
      return;
    }

    // 5. OBI check (sell dominance for mean reversion)
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

    // 5.5. OFI check (Order Flow Imbalance - sell pressure weakening?)
    double ofi = this->feature_engine_->get_ofi();
    if (ofi < 0) {
      // Negative OFI: Ask qty increasing (sell pressure still building - risky!)
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[Entry Block] Long | Sell flow still strong | z:{:.2f} | "
            "ofi:{:.2f}",
            z_robust,
            ofi);
      }
      return;
    }

    // 5.6. Reversal momentum check (buy pressure building?)
    if (!check_reversal_momentum(common::Side::kBuy)) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[Entry Block] Long | Insufficient buy momentum | z:{:.2f}",
            z_robust);
      }
      return;
    }

    // 6. Spread filter
    double spread =
        (bbo->ask_price.value - bbo->bid_price.value) / bbo->bid_price.value;
    if (spread < entry_cfg_.min_spread_filter) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[Entry Block] Long | Spread too small | z:{:.2f} | spread:{:.4f}% "
            "< "
            "{:.4f}%",
            z_robust,
            spread * 100,
            entry_cfg_.min_spread_filter * 100);
      }
      return;
    }

    // 7. Set position to PENDING state BEFORE sending order
    long_position_.status = PositionStatus::PENDING;
    long_position_.qty = entry_cfg_.position_size;
    long_position_.entry_price = bbo->bid_price.value;
    long_position_.entry_wall_info = bid_wall_info_;
    long_position_.state_time = get_current_time_ns();

    // 8. Execute entry (OrderId stored internally)
    place_entry_order(common::Side::kBuy, bbo->bid_price.value);

    if (debug_cfg_.log_entry_exit) {
      this->logger_.info(
          "[Entry Signal] LONG | quality:{:.2f} ({}) | z_robust:{:.2f} | "
          "price:{} | wall:${:.0f}@{:.4f}% | obi:{:.2f} | ofi:{:.2f} | "
          "components: z={:.2f} wall={:.2f} vol={:.2f} obi={:.2f}",
          composite,
          signal.get_quality(entry_cfg_) == SignalScore::Quality::EXCELLENT
              ? "EXCELLENT"
              : "GOOD",
          z_robust,
          bbo->bid_price.value,
          bid_wall_info_.accumulated_amount,
          bid_wall_info_.distance_pct * 100,
          obi,
          ofi,
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
      const BBO* bbo, double z_robust) {
    // Z-score is passed as parameter to avoid redundant calculation

    // 1. Calculate Multi-Factor Signal Score
    double obi = calculate_orderbook_imbalance(order_book);
    SignalScore signal =
        calculate_short_signal_score(z_robust, ask_wall_info_, obi);
    double composite = signal.composite(entry_cfg_);

    // Check signal quality threshold
    if (composite < entry_cfg_.min_signal_quality) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[Entry Block] SHORT | Signal quality too low | "
            "score:{:.2f} < {:.2f} | z:{:.2f} wall:{:.2f} vol:{:.2f} "
            "obi:{:.2f}",
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
    if (z_robust < zscore_entry_threshold_ * 0.8) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[Entry Block] Short | Already dropped too much | z:{:.2f} < "
            "{:.2f}",
            z_robust,
            zscore_entry_threshold_ * 0.8);
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

    // 4. Trend acceleration filter (SAFETY)
    if (this->feature_engine_->is_trend_accelerating(common::Side::kBuy,
            trend_cfg_.lookback_ticks,
            trend_cfg_.consecutive_threshold,
            trend_cfg_.volume_multiplier)) {
      if (debug_cfg_.log_entry_exit) {
        const auto* trades = this->feature_engine_->get_recent_trades();
        const size_t trade_count =
            this->feature_engine_->get_trade_history_size();
        int buy_count = 0;
        size_t count = std::min(trade_count,
            static_cast<size_t>(trend_cfg_.lookback_ticks));
        for (size_t i = 0; i < count; ++i) {
          size_t idx = trade_count - count + i;
          if (trades[idx].side == common::Side::kBuy)
            buy_count++;
        }
        this->logger_.info(
            "[Entry Block] Short | Trend accelerating | z:{:.2f} | buys:{}/{}",
            z_robust,
            buy_count,
            count);
      }
      return;
    }

    // 5. OBI check (buy dominance for mean reversion)
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

    // 5.5. OFI check (Order Flow Imbalance - buy pressure weakening?)
    double ofi = this->feature_engine_->get_ofi();
    if (ofi > 0) {
      // Positive OFI: Bid qty increasing (buy pressure still building - risky!)
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[Entry Block] Short | Buy flow still strong | z:{:.2f} | "
            "ofi:{:.2f}",
            z_robust,
            ofi);
      }
      return;
    }

    // 5.6. Reversal momentum check (sell pressure building?)
    if (!check_reversal_momentum(common::Side::kSell)) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[Entry Block] Short | Insufficient sell momentum | z:{:.2f}",
            z_robust);
      }
      return;
    }

    // 6. Spread filter
    double spread =
        (bbo->ask_price.value - bbo->bid_price.value) / bbo->bid_price.value;
    if (spread < entry_cfg_.min_spread_filter) {
      if (debug_cfg_.log_entry_exit) {
        this->logger_.info(
            "[Entry Block] Short | Spread too small | z:{:.2f} | "
            "spread:{:.4f}% < "
            "{:.4f}%",
            z_robust,
            spread * 100,
            entry_cfg_.min_spread_filter * 100);
      }
      return;
    }

    // 7. Set position to PENDING state BEFORE sending order
    short_position_.status = PositionStatus::PENDING;
    short_position_.qty = entry_cfg_.position_size;
    short_position_.entry_price = bbo->ask_price.value;
    short_position_.entry_wall_info = ask_wall_info_;
    short_position_.state_time = get_current_time_ns();

    // 8. Execute entry (OrderId stored internally)
    place_entry_order(common::Side::kSell, bbo->ask_price.value);

    if (debug_cfg_.log_entry_exit) {
      this->logger_.info(
          "[Entry Signal] SHORT | quality:{:.2f} ({}) | z_robust:{:.2f} | "
          "price:{} | wall:${:.0f}@{:.4f}% | obi:{:.2f} | ofi:{:.2f} | "
          "components: z={:.2f} wall={:.2f} vol={:.2f} obi={:.2f}",
          composite,
          signal.get_quality(entry_cfg_) == SignalScore::Quality::EXCELLENT
              ? "EXCELLENT"
              : "GOOD",
          z_robust,
          bbo->ask_price.value,
          ask_wall_info_.accumulated_amount,
          ask_wall_info_.distance_pct * 100,
          obi,
          ofi,
          signal.z_score_strength,
          signal.wall_strength,
          signal.volume_strength,
          signal.obi_strength);
    }
  }

  // ========================================
  // Order execution
  // ========================================
  void place_entry_order(common::Side side, double base_price) {
    QuoteIntentType intent{};
    intent.ticker = ticker_;
    intent.side = side;

    if (side == common::Side::kBuy) {
      intent.price = common::Price{base_price - entry_cfg_.safety_margin};
      if constexpr (SelectedOeTraits::supports_position_side()) {
        intent.position_side = common::PositionSide::kLong;
      }
    } else {
      intent.price = common::Price{base_price + entry_cfg_.safety_margin};
      if constexpr (SelectedOeTraits::supports_position_side()) {
        intent.position_side = common::PositionSide::kShort;
      }
    }

    intent.qty = Qty{entry_cfg_.position_size};

    if (debug_cfg_.log_entry_exit) {
      this->logger_.info(
          "[Order Sent] {} | base_price:{} | margin:{} | order_price:{} | "
          "qty:{}",
          side == common::Side::kBuy ? "BUY" : "SELL",
          base_price,
          entry_cfg_.safety_margin,
          intent.price.value().value,
          entry_cfg_.position_size);
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
    double mid_price = (bbo->bid_price.value + bbo->ask_price.value) * 0.5;
    double current_z = robust_zscore_mid_->calculate_zscore(mid_price);
    double current_obi = calculate_orderbook_imbalance(order_book);

    check_long_exit(bbo, mid_price, current_z, current_obi);
    check_short_exit(bbo, mid_price, current_z, current_obi);
  }

  // ========================================
  // Long position exit
  // ========================================
  void check_long_exit(const BBO* bbo, double mid_price, double current_z,
      double current_obi) {
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
    else if (current_z >= -exit_cfg_.zscore_exit_threshold) {
      should_exit = true;
      reason = "Z-score mean reversion";
    }

    // Priority 5: Wall decay
    else if (bid_wall_info_.accumulated_amount <
             long_position_.entry_wall_info.accumulated_amount *
                 exit_cfg_.wall_amount_decay_ratio) {
      should_exit = true;
      reason = "Bid wall decayed";
    }

    // Priority 6: Wall distance expansion
    else if (bid_wall_info_.distance_pct >
             long_position_.entry_wall_info.distance_pct *
                 exit_cfg_.wall_distance_expand_ratio) {
      should_exit = true;
      reason = "Bid wall moved away";
    }

    // Priority 7: Stop loss
    else if ((mid_price - long_position_.entry_price) /
                 long_position_.entry_price <
             -exit_cfg_.max_loss_pct) {
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
  void check_short_exit(const BBO* bbo, double mid_price, double current_z,
      double current_obi) {
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
    else if (current_z <= exit_cfg_.zscore_exit_threshold) {
      should_exit = true;
      reason = "Z-score mean reversion";
    }

    // Priority 5: Wall decay
    else if (ask_wall_info_.accumulated_amount <
             short_position_.entry_wall_info.accumulated_amount *
                 exit_cfg_.wall_amount_decay_ratio) {
      should_exit = true;
      reason = "Ask wall decayed";
    }

    // Priority 6: Wall distance expansion
    else if (ask_wall_info_.distance_pct >
             short_position_.entry_wall_info.distance_pct *
                 exit_cfg_.wall_distance_expand_ratio) {
      should_exit = true;
      reason = "Ask wall moved away";
    }

    // Priority 7: Stop loss
    else if ((short_position_.entry_price - mid_price) /
                 short_position_.entry_price <
             -exit_cfg_.max_loss_pct) {
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
      double market_price, const std::string& reason) {
    QuoteIntentType intent{};
    intent.ticker = ticker_;
    intent.side = exit_side;

    if (exit_side == common::Side::kSell) {
      intent.qty = Qty{long_position_.qty};
    } else {
      intent.qty = Qty{short_position_.qty};
    }

    // Taker mode
    intent.price = common::Price{market_price};

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
          market_price);
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
  void update_long_phase(double current_z) {
    // Calculate adaptive threshold (using mid-term timeframe)
    double adaptive_threshold =
        robust_zscore_mid_->get_adaptive_threshold(zscore_entry_threshold_);

    double z_abs = std::abs(current_z);

    switch (long_phase_) {
      case ReversionPhase::NEUTRAL:
        // Enter BUILDING_OVERSOLD when crossing neutral zone
        if (current_z < -mean_reversion_cfg_.neutral_zone_threshold) {
          long_phase_ = ReversionPhase::BUILDING_OVERSOLD;
          oversold_min_z_ = current_z;

          if (debug_cfg_.log_entry_exit) {
            this->logger_.info(
                "[MeanReversion] Long BUILDING_OVERSOLD | z:{:.2f} | "
                "threshold:{:.2f}",
                current_z,
                adaptive_threshold);
          }
        }
        break;

      case ReversionPhase::BUILDING_OVERSOLD:
        oversold_min_z_ = std::min(oversold_min_z_, current_z);

        // Enter DEEP_OVERSOLD when crossing deep threshold
        if (z_abs > adaptive_threshold * mean_reversion_cfg_.deep_multiplier) {
          long_phase_ = ReversionPhase::DEEP_OVERSOLD;

          if (debug_cfg_.log_entry_exit) {
            this->logger_.info(
                "[MeanReversion] Long DEEP_OVERSOLD | z:{:.2f} | "
                "deep_threshold:{:.2f}",
                current_z,
                adaptive_threshold * mean_reversion_cfg_.deep_multiplier);
          }
        }
        // Return to NEUTRAL if going back above neutral zone
        else if (current_z > -mean_reversion_cfg_.neutral_zone_threshold) {
          long_phase_ = ReversionPhase::NEUTRAL;
          oversold_min_z_ = 0.0;

          if (debug_cfg_.log_entry_exit) {
            this->logger_.info(
                "[MeanReversion] Long reset to NEUTRAL | z:{:.2f}",
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
          if (z_abs > adaptive_threshold *
                          mean_reversion_cfg_.reversal_weak_multiplier) {
            long_phase_ = ReversionPhase::REVERSAL_WEAK;

            if (debug_cfg_.log_entry_exit) {
              this->logger_.info(
                  "[MeanReversion] 🔸 Long REVERSAL_WEAK | "
                  "min_z:{:.2f} → current_z:{:.2f} | bounce:{:.2f}",
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
                  "[MeanReversion] 🚨 Long REVERSAL_STRONG | "
                  "min_z:{:.2f} → current_z:{:.2f} | bounce:{:.2f} | wall:{}",
                  oversold_min_z_,
                  current_z,
                  current_z - oversold_min_z_,
                  bid_wall_info_.is_valid ? "YES" : "NO");
            }
          }
        }
        // Dropped back to BUILDING level
        else if (z_abs <
                 adaptive_threshold * mean_reversion_cfg_.deep_multiplier) {
          long_phase_ = ReversionPhase::BUILDING_OVERSOLD;

          if (debug_cfg_.log_entry_exit) {
            this->logger_.info(
                "[MeanReversion] Long back to BUILDING | z:{:.2f}",
                current_z);
          }
        }
        break;

      case ReversionPhase::REVERSAL_WEAK:
        // Re-check threshold (adaptive_threshold may have changed!)
        if (z_abs <
            adaptive_threshold * mean_reversion_cfg_.reversal_weak_multiplier) {
          long_phase_ = ReversionPhase::REVERSAL_STRONG;

          if (debug_cfg_.log_entry_exit) {
            this->logger_.info(
                "[MeanReversion] 🚨 Long WEAK → STRONG | z:{:.2f} | "
                "threshold:{:.2f}",
                current_z,
                adaptive_threshold *
                    mean_reversion_cfg_.reversal_weak_multiplier);
          }
        }
        // Falling back to DEEP_OVERSOLD
        else if (current_z < oversold_min_z_ -
                                 mean_reversion_cfg_.min_reversal_bounce *
                                     mean_reversion_cfg_.false_reversal_ratio) {
          long_phase_ = ReversionPhase::DEEP_OVERSOLD;
          oversold_min_z_ = current_z;

          if (debug_cfg_.log_entry_exit) {
            this->logger_.info(
                "[MeanReversion] Long WEAK → DEEP (false reversal) | z:{:.2f}",
                current_z);
          }
        }
        // Return to neutral
        else if (current_z > -mean_reversion_cfg_.neutral_zone_threshold) {
          long_phase_ = ReversionPhase::NEUTRAL;
          oversold_min_z_ = 0.0;
        }
        break;

      case ReversionPhase::REVERSAL_STRONG:
        // Only allow entry from this state
        // Reset after entry or return to neutral
        if (long_position_.status != PositionStatus::NONE ||
            current_z > -mean_reversion_cfg_.neutral_zone_threshold) {
          long_phase_ = ReversionPhase::NEUTRAL;
          oversold_min_z_ = 0.0;

          if (debug_cfg_.log_entry_exit) {
            this->logger_.info(
                "[MeanReversion] Long reset | z:{:.2f} | position:{}",
                current_z,
                long_position_.status == PositionStatus::NONE ? "NONE"
                                                              : "ACTIVE");
          }
        }
        // Falling back to WEAK (reversal weakening)
        else if (z_abs > adaptive_threshold *
                             mean_reversion_cfg_.reversal_weak_multiplier) {
          long_phase_ = ReversionPhase::REVERSAL_WEAK;

          if (debug_cfg_.log_entry_exit) {
            this->logger_.info(
                "[MeanReversion] Long STRONG → WEAK (reversal weakening) | "
                "z:{:.2f}",
                current_z);
          }
        }
        break;
    }
  }

  void update_short_phase(double current_z) {
    // Calculate adaptive threshold (using mid-term timeframe)
    double adaptive_threshold =
        robust_zscore_mid_->get_adaptive_threshold(zscore_entry_threshold_);

    double z_abs = std::abs(current_z);

    switch (short_phase_) {
      case ReversionPhase::NEUTRAL:
        // Enter BUILDING (overbought) when crossing neutral zone
        if (current_z > mean_reversion_cfg_.neutral_zone_threshold) {
          short_phase_ =
              ReversionPhase::BUILDING_OVERSOLD;  // Reusing for overbought
          overbought_max_z_ = current_z;

          if (debug_cfg_.log_entry_exit) {
            this->logger_.info(
                "[MeanReversion] Short BUILDING_OVERBOUGHT | z:{:.2f} | "
                "threshold:{:.2f}",
                current_z,
                adaptive_threshold);
          }
        }
        break;

      case ReversionPhase::BUILDING_OVERSOLD:  // Actually overbought for Short
        overbought_max_z_ = std::max(overbought_max_z_, current_z);

        // Enter DEEP_OVERBOUGHT when crossing deep threshold
        if (z_abs > adaptive_threshold * mean_reversion_cfg_.deep_multiplier) {
          short_phase_ =
              ReversionPhase::DEEP_OVERSOLD;  // Reusing for deep overbought

          if (debug_cfg_.log_entry_exit) {
            this->logger_.info(
                "[MeanReversion] Short DEEP_OVERBOUGHT | z:{:.2f} | "
                "deep_threshold:{:.2f}",
                current_z,
                adaptive_threshold * mean_reversion_cfg_.deep_multiplier);
          }
        }
        // Return to NEUTRAL
        else if (current_z < mean_reversion_cfg_.neutral_zone_threshold) {
          short_phase_ = ReversionPhase::NEUTRAL;
          overbought_max_z_ = 0.0;

          if (debug_cfg_.log_entry_exit) {
            this->logger_.info(
                "[MeanReversion] Short reset to NEUTRAL | z:{:.2f}",
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
          if (z_abs > adaptive_threshold *
                          mean_reversion_cfg_.reversal_weak_multiplier) {
            short_phase_ = ReversionPhase::REVERSAL_WEAK;

            if (debug_cfg_.log_entry_exit) {
              this->logger_.info(
                  "[MeanReversion] 🔸 Short REVERSAL_WEAK | "
                  "max_z:{:.2f} → current_z:{:.2f} | drop:{:.2f}",
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
                  "[MeanReversion] 🚨 Short REVERSAL_STRONG | "
                  "max_z:{:.2f} → current_z:{:.2f} | drop:{:.2f} | wall:{}",
                  overbought_max_z_,
                  current_z,
                  overbought_max_z_ - current_z,
                  ask_wall_info_.is_valid ? "YES" : "NO");
            }
          }
        }
        // Rose back to BUILDING level
        else if (z_abs <
                 adaptive_threshold * mean_reversion_cfg_.deep_multiplier) {
          short_phase_ = ReversionPhase::BUILDING_OVERSOLD;

          if (debug_cfg_.log_entry_exit) {
            this->logger_.info(
                "[MeanReversion] Short back to BUILDING | z:{:.2f}",
                current_z);
          }
        }
        break;

      case ReversionPhase::REVERSAL_WEAK:
        // Re-check threshold (adaptive_threshold may have changed!)
        if (z_abs <
            adaptive_threshold * mean_reversion_cfg_.reversal_weak_multiplier) {
          short_phase_ = ReversionPhase::REVERSAL_STRONG;

          if (debug_cfg_.log_entry_exit) {
            this->logger_.info(
                "[MeanReversion] 🚨 Short WEAK → STRONG | z:{:.2f} | "
                "threshold:{:.2f}",
                current_z,
                adaptive_threshold *
                    mean_reversion_cfg_.reversal_weak_multiplier);
          }
        }
        // Rising back to DEEP_OVERBOUGHT
        else if (current_z > overbought_max_z_ +
                                 mean_reversion_cfg_.min_reversal_bounce *
                                     mean_reversion_cfg_.false_reversal_ratio) {
          short_phase_ = ReversionPhase::DEEP_OVERSOLD;
          overbought_max_z_ = current_z;

          if (debug_cfg_.log_entry_exit) {
            this->logger_.info(
                "[MeanReversion] Short WEAK → DEEP (false reversal) | z:{:.2f}",
                current_z);
          }
        }
        // Return to neutral
        else if (current_z < mean_reversion_cfg_.neutral_zone_threshold) {
          short_phase_ = ReversionPhase::NEUTRAL;
          overbought_max_z_ = 0.0;
        }
        break;

      case ReversionPhase::REVERSAL_STRONG:
        // Only allow entry from this state
        // Reset after entry or return to neutral
        if (short_position_.status != PositionStatus::NONE ||
            current_z < mean_reversion_cfg_.neutral_zone_threshold) {
          short_phase_ = ReversionPhase::NEUTRAL;
          overbought_max_z_ = 0.0;

          if (debug_cfg_.log_entry_exit) {
            this->logger_.info(
                "[MeanReversion] Short reset | z:{:.2f} | position:{}",
                current_z,
                short_position_.status == PositionStatus::NONE ? "NONE"
                                                               : "ACTIVE");
          }
        }
        // Rising back to WEAK (reversal weakening)
        else if (z_abs > adaptive_threshold *
                             mean_reversion_cfg_.reversal_weak_multiplier) {
          short_phase_ = ReversionPhase::REVERSAL_WEAK;

          if (debug_cfg_.log_entry_exit) {
            this->logger_.info(
                "[MeanReversion] Short STRONG → WEAK (reversal weakening) | "
                "z:{:.2f}",
                current_z);
          }
        }
        break;
    }
  }

  // ========================================
  // Multi-Factor Signal Scoring
  // ========================================

  /**
   * @brief Calculate volume reversal score (0-1)
   * @param expected_direction Expected trade direction for reversal
   * @return Normalized score combining tick ratio and volume ratio
   */
  double calculate_volume_reversal_score(
      common::Side expected_direction) const {
    const auto* trades = this->feature_engine_->get_recent_trades();
    const size_t trade_count = this->feature_engine_->get_trade_history_size();

    const int lookback = entry_cfg_.volume_score_lookback;
    if (trade_count < static_cast<size_t>(lookback))
      return 0.0;

    int directional_count = 0;
    double directional_volume = 0.0;
    double total_volume = 0.0;

    // Analyze recent trades
    for (size_t i = trade_count - lookback; i < trade_count; ++i) {
      if (trades[i].side == expected_direction) {
        directional_count++;
        directional_volume += trades[i].qty;
      }
      total_volume += trades[i].qty;
    }

    if (total_volume < 1e-8)
      return 0.0;

    // Combine tick ratio and volume ratio
    double tick_ratio =
        static_cast<double>(directional_count) / static_cast<double>(lookback);
    double volume_ratio = directional_volume / total_volume;

    return (tick_ratio + volume_ratio) / 2.0;  // Average of both metrics
  }

  /**
   * @brief Calculate long entry signal score
   * @param z Z-score value
   * @param wall Wall information
   * @param obi Orderbook imbalance
   * @return SignalScore with all components normalized to 0-1
   */
  SignalScore calculate_long_signal_score(double z,
      const FeatureEngineT::WallInfo& wall, double obi) const {
    SignalScore score;

    // === 1. Z-score component: normalize to 0-1 ===
    // Example: z=-2.0 → 0.0, z=-2.5 → 0.5, z=-3.0 → 1.0
    double z_abs = std::abs(z);
    double z_range = entry_cfg_.zscore_norm_max - entry_cfg_.zscore_norm_min;
    score.z_score_strength =
        std::clamp((z_abs - entry_cfg_.zscore_norm_min) / z_range, 0.0, 1.0);

    // === 2. Wall strength: compare to dynamic threshold ===
    // Example: threshold=30k, wall=15k → 0.25, wall=60k → 1.0
    double wall_target = dynamic_threshold_->get_min_quantity() *
                         entry_cfg_.wall_norm_multiplier;
    score.wall_strength =
        std::clamp(wall.accumulated_amount / wall_target, 0.0, 1.0);

    // === 3. Volume reversal: calculate directional strength ===
    score.volume_strength = calculate_volume_reversal_score(common::Side::kBuy);

    // === 4. OBI strength: normalize to 0-1 ===
    // Long: OBI should be negative (sell pressure) but not too extreme
    // Example: OBI=-0.05 → 0.0, OBI=-0.15 → 0.5, OBI=-0.25 → 1.0
    double obi_abs = std::abs(obi);
    double obi_range = entry_cfg_.obi_norm_max - entry_cfg_.obi_norm_min;
    score.obi_strength =
        std::clamp((obi_abs - entry_cfg_.obi_norm_min) / obi_range, 0.0, 1.0);

    return score;
  }

  /**
   * @brief Calculate short entry signal score
   * @param z Z-score value
   * @param wall Wall information
   * @param obi Orderbook imbalance
   * @return SignalScore with all components normalized to 0-1
   */
  SignalScore calculate_short_signal_score(double z,
      const FeatureEngineT::WallInfo& wall, double obi) const {
    SignalScore score;

    // === 1. Z-score component ===
    double z_abs = std::abs(z);
    double z_range = entry_cfg_.zscore_norm_max - entry_cfg_.zscore_norm_min;
    score.z_score_strength =
        std::clamp((z_abs - entry_cfg_.zscore_norm_min) / z_range, 0.0, 1.0);

    // === 2. Wall strength ===
    double wall_target = dynamic_threshold_->get_min_quantity() *
                         entry_cfg_.wall_norm_multiplier;
    score.wall_strength =
        std::clamp(wall.accumulated_amount / wall_target, 0.0, 1.0);

    // === 3. Volume reversal ===
    score.volume_strength =
        calculate_volume_reversal_score(common::Side::kSell);

    // === 4. OBI strength ===
    // Short: OBI should be positive (buy pressure) but not too extreme
    double obi_abs = std::abs(obi);
    double obi_range = entry_cfg_.obi_norm_max - entry_cfg_.obi_norm_min;
    score.obi_strength =
        std::clamp((obi_abs - entry_cfg_.obi_norm_min) / obi_range, 0.0, 1.0);

    return score;
  }

  // ========================================
  // Member variables
  // ========================================
  // Config parameters (grouped)
  const bool allow_simultaneous_positions_;
  const double defense_qty_multiplier_;
  const double zscore_entry_threshold_;

  const WallDetectionConfig wall_cfg_;
  const EntryConfig entry_cfg_;
  const ExitConfig exit_cfg_;
  const TrendFilterConfig trend_cfg_;
  const ReversalMomentumConfig reversal_cfg_;
  const DebugLoggingConfig debug_cfg_;
  const MeanReversionConfig mean_reversion_cfg_;

  // Z-score config (kept separate for module initialization)
  const int zscore_window_size_;
  const int zscore_min_samples_;
  const double zscore_min_mad_threshold_;

  // Multi-timeframe Z-score config
  const int zscore_fast_window_;
  const int zscore_fast_min_samples_;
  const int zscore_slow_window_;
  const int zscore_slow_min_samples_;
  const double zscore_slow_threshold_;

  // Dynamic state
  common::TickerId ticker_;
  FeatureEngineT::WallInfo bid_wall_info_;
  FeatureEngineT::WallInfo ask_wall_info_;
  PositionState long_position_;
  PositionState short_position_;
  BBO prev_bbo_;

  // OBI calculation buffers
  std::vector<double> bid_qty_;
  std::vector<double> ask_qty_;

  // Wall detection buffers (reused to avoid allocation)
  std::vector<double> wall_level_qty_;
  std::vector<int> wall_level_idx_;

  // Dynamic threshold
  double current_wall_threshold_;
  std::unique_ptr<DynamicWallThreshold> dynamic_threshold_;

  // Robust Z-score modules (multi-timeframe)
  std::unique_ptr<RobustZScore> robust_zscore_fast_;  // ~1 sec (10 ticks)
  std::unique_ptr<RobustZScore> robust_zscore_mid_;   // ~5 sec (30 ticks)
  std::unique_ptr<RobustZScore> robust_zscore_slow_;  // ~30 sec (100 ticks)

  // Note: Trade history is now managed by FeatureEngine::recent_trades_

  // Reversal confirmation tracking
  double prev_z_score_{0.0};

  // Mean reversion phase tracking (Simplified)
  ReversionPhase long_phase_{ReversionPhase::NEUTRAL};
  ReversionPhase short_phase_{ReversionPhase::NEUTRAL};
  double oversold_min_z_{0.0};    // Minimum Z-Score reached in oversold
  double overbought_max_z_{0.0};  // Maximum Z-Score reached in overbought

  // Throttling timestamp for orderbook updates
  uint64_t last_orderbook_check_time_{0};
};

}  // namespace trading

#endif  // MEAN_REVERSION_MAKER_H
