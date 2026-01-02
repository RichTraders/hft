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
            INI_CONFIG.get_double("entry", "min_spread_filter", 0.0004)},

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

        // === Z-score config ===
        zscore_window_size_(
            INI_CONFIG.get_int("robust_zscore", "window_size", 30)),
        zscore_min_samples_(
            INI_CONFIG.get_int("robust_zscore", "min_samples", 20)),
        zscore_min_mad_threshold_(
            INI_CONFIG.get_double("robust_zscore", "min_mad_threshold", 5.0)),

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

        // === Robust Z-score module ===
        robust_zscore_(std::make_unique<RobustZScore>(
            RobustZScoreConfig{zscore_window_size_,
                zscore_min_samples_,
                zscore_min_mad_threshold_})) {
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
    bid_wall_info_ = this->feature_engine_->detect_wall(order_book,
        common::Side::kBuy,
        wall_cfg_.max_levels,
        current_wall_threshold_,
        wall_cfg_.max_distance_pct,
        min_price_int,
        wall_level_qty_,
        wall_level_idx_);
    allow_long_entry_ = bid_wall_info_.is_valid;

    ask_wall_info_ = this->feature_engine_->detect_wall(order_book,
        common::Side::kSell,
        wall_cfg_.max_levels,
        current_wall_threshold_,
        wall_cfg_.max_distance_pct,
        min_price_int,
        wall_level_qty_,
        wall_level_idx_);
    allow_short_entry_ = ask_wall_info_.is_valid;

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

    // === 1. Update price statistics ===
    uint64_t current_time = get_current_time_ns();

    // Update Robust Z-score
    robust_zscore_->on_price(market_data->price.value);

    // Note: Trade history is now managed by FeatureEngine

    // Accumulate trade volume for wall threshold (EMA update)
    dynamic_threshold_->on_trade(current_time,
        market_data->price.value,
        market_data->qty.value);

    // === Calculate Z-score once (performance optimization) ===
    double current_z =
        robust_zscore_->calculate_zscore(market_data->price.value);

    // === 2. Long entry check (Reversal Confirmation Strategy) ===
    // Enter LONG when: oversold -> reversing up -> buy trade occurs
    bool was_oversold = (prev_z_score_ < -zscore_entry_threshold_);
    bool is_reversing_up = (current_z > prev_z_score_);

    if (market_data->side ==
            common::Side::kBuy &&  // Buy trade (reversal signal)
        was_oversold &&            // Was oversold before
        is_reversing_up &&         // Reversing up now
        allow_long_entry_ &&
        (long_position_.status == PositionStatus::NONE &&
            (allow_simultaneous_positions_ ||
                short_position_.status == PositionStatus::NONE))) {

      if (validate_defense_realtime(market_data,
              prev_bbo_,
              current_bbo,
              common::Side::kBuy)) {
        check_long_entry(market_data, order_book, current_bbo, current_z);
      }
    }

    // === 3. Short entry check (Reversal Confirmation Strategy) ===
    // Enter SHORT when: overbought -> reversing down -> sell trade occurs
    bool was_overbought = (prev_z_score_ > zscore_entry_threshold_);
    bool is_reversing_down = (current_z < prev_z_score_);

    if (market_data->side ==
            common::Side::
                kSell &&      // [CRITICAL CHANGE] Sell trade (reversal signal)
        was_overbought &&     // [NEW] Was overbought before
        is_reversing_down &&  // [NEW] Reversing down now
        allow_short_entry_ &&
        (short_position_.status == PositionStatus::NONE &&
            (allow_simultaneous_positions_ ||
                long_position_.status == PositionStatus::NONE))) {

      if (validate_defense_realtime(market_data,
              prev_bbo_,
              current_bbo,
              common::Side::kSell)) {
        check_short_entry(market_data, order_book, current_bbo, current_z);
      }
    }

    // === 4. Save state for next tick ===
    prev_bbo_ = *current_bbo;
    prev_z_score_ = current_z;

    // === 5. Trigger TTL sweep (every trade) ===
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
          robust_zscore_->get_median(),
          robust_zscore_->get_mad(),
          z_robust);
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
    double obi = calculate_orderbook_imbalance(order_book);
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
          "[Entry Signal] LONG | z_robust:{:.2f} | price:{} | "
          "wall:${:.0f}@{:.4f}% | obi:{:.2f} | ofi:{:.2f}",
          z_robust,
          bbo->bid_price.value,
          bid_wall_info_.accumulated_amount,
          bid_wall_info_.distance_pct * 100,
          obi,
          ofi);
    }
  }

  // ========================================
  // Short entry
  // ========================================
  void check_short_entry(const MarketData*, MarketOrderBookT* order_book,
      const BBO* bbo, double z_robust) {
    // Z-score is passed as parameter to avoid redundant calculation

    // 1. Check if still in overbought territory (but declining)
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
    double obi = calculate_orderbook_imbalance(order_book);
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
          "[Entry Signal] SHORT | z_robust:{:.2f} | price:{} | "
          "wall:${:.0f}@{:.4f}% | obi:{:.2f} | ofi:{:.2f}",
          z_robust,
          bbo->ask_price.value,
          ask_wall_info_.accumulated_amount,
          ask_wall_info_.distance_pct * 100,
          obi,
          ofi);
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
    double current_z = robust_zscore_->calculate_zscore(mid_price);
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

  // Z-score config (kept separate for module initialization)
  const int zscore_window_size_;
  const int zscore_min_samples_;
  const double zscore_min_mad_threshold_;

  // Dynamic state
  common::TickerId ticker_;
  FeatureEngineT::WallInfo bid_wall_info_;
  FeatureEngineT::WallInfo ask_wall_info_;
  bool allow_long_entry_{false};
  bool allow_short_entry_{false};
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

  // Robust Z-score module
  std::unique_ptr<RobustZScore> robust_zscore_;

  // Note: Trade history is now managed by FeatureEngine::recent_trades_

  // Reversal confirmation tracking
  double prev_z_score_{0.0};

  // Throttling timestamp for orderbook updates
  uint64_t last_orderbook_check_time_{0};
};

}  // namespace trading

#endif  // MEAN_REVERSION_MAKER_H
