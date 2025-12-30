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

class MeanReversionMakerStrategy
    : public BaseStrategy<MeanReversionMakerStrategy> {
 public:
  using QuoteIntentType =
      std::conditional_t<SelectedOeTraits::supports_position_side(),
          FuturesQuoteIntent, SpotQuoteIntent>;
  using OrderManagerT = OrderManager<MeanReversionMakerStrategy>;
  using FeatureEngineT = FeatureEngine<MeanReversionMakerStrategy>;
  using MarketOrderBookT = MarketOrderBook<MeanReversionMakerStrategy>;

  // === Wall information structure ===
  struct WallInfo {
    double accumulated_amount{0.0};
    double distance_pct{0.0};
    int levels_checked{0};
    bool is_valid{false};
  };

  // === Position state structure ===
  struct PositionState {
    double qty{0.0};
    double entry_price{0.0};
    WallInfo entry_wall_info;
    bool is_active{false};
  };

  // === Trade record for trend detection ===
  struct TradeRecord {
    common::Side side;
    double qty;
    uint64_t timestamp;
  };

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

        // === Wall detection ===
        wall_max_distance_pct_(
            INI_CONFIG.get_double("strategy", "wall_max_distance_pct", 0.0015)),
        wall_max_levels_(
            INI_CONFIG.get_int("strategy", "wall_max_levels", 100)),

        // === Defense ===
        defense_qty_multiplier_(
            INI_CONFIG.get_double("strategy", "defense_qty_multiplier", 2.0)),

        // === Entry ===
        entry_obi_threshold_(
            INI_CONFIG.get_double("strategy", "entry_obi_threshold", 0.25)),
        entry_obi_levels_(
            INI_CONFIG.get_int("strategy", "entry_obi_levels", 5)),
        position_size_(
            INI_CONFIG.get_double("strategy", "position_size", 0.01)),
        entry_safety_margin_(
            INI_CONFIG.get_double("strategy", "entry_safety_margin", 0.00005)),
        min_spread_filter_(
            INI_CONFIG.get_double("strategy", "min_spread_filter", 0.0004)),

        // === Exit ===
        wall_amount_decay_ratio_(
            INI_CONFIG.get_double("strategy", "wall_amount_decay_ratio", 0.5)),
        wall_distance_expand_ratio_(INI_CONFIG.get_double("strategy",
            "wall_distance_expand_ratio", 1.2)),
        max_loss_pct_(INI_CONFIG.get_double("strategy", "max_loss_pct", 0.002)),

        // === Logging ===
        log_wall_detection_(INI_CONFIG.get("strategy", "log_wall_detection",
                                "false") == "true"),
        log_defense_check_(
            INI_CONFIG.get("strategy", "log_defense_check", "false") == "true"),
        log_entry_exit_(
            INI_CONFIG.get("strategy", "log_entry_exit", "false") == "true"),

        // === Robust Z-score ===
        zscore_window_size_(
            INI_CONFIG.get_int("strategy", "zscore_window_size", 30)),
        zscore_min_samples_(
            INI_CONFIG.get_int("strategy", "zscore_min_samples", 20)),
        zscore_entry_threshold_(
            INI_CONFIG.get_double("strategy", "zscore_entry_threshold", 2.5)),

        // === Trend Filter ===
        trend_filter_enabled_(INI_CONFIG.get("strategy", "trend_filter_enabled",
                                  "true") == "true"),
        trend_lookback_ticks_(
            INI_CONFIG.get_int("strategy", "trend_lookback_ticks", 5)),
        trend_consecutive_threshold_(
            INI_CONFIG.get_int("strategy", "trend_consecutive_threshold", 4)),
        trend_volume_multiplier_(
            INI_CONFIG.get_double("strategy", "trend_volume_multiplier", 1.5)),

        // === OBI calculation buffers ===
        bid_qty_(entry_obi_levels_),
        ask_qty_(entry_obi_levels_),
        current_wall_threshold_(0.0),

        // === Dynamic threshold module ===
        dynamic_threshold_(std::make_unique<DynamicWallThreshold>(
            INI_CONFIG.get_double("strategy", "wall_volume_ema_alpha", 0.03),
            INI_CONFIG.get_double("strategy", "wall_volume_multiplier", 4.0),
            INI_CONFIG.get_int("strategy", "wall_volume_min_samples", 20),
            INI_CONFIG.get_int("strategy", "wall_orderbook_top_levels", 20),
            INI_CONFIG.get_double("strategy", "wall_orderbook_multiplier", 3.0),
            INI_CONFIG.get_double("strategy", "wall_orderbook_percentile", 80),
            INI_CONFIG.get_double("strategy", "wall_volume_weight", 0.7),
            INI_CONFIG.get_double("strategy", "wall_orderbook_weight", 0.3),
            INI_CONFIG.get_double("strategy", "wall_min_quantity", 3.5))),

        // === Robust Z-score module ===
        robust_zscore_(std::make_unique<RobustZScore>(zscore_window_size_,
            zscore_min_samples_)) {
    this->logger_.info(
        "[MeanReversionMaker] Initialized | min_quantity:{:.2f} BTC | "
        "simultaneous:{} | trend_filter:{}",
        INI_CONFIG.get_double("strategy", "wall_min_quantity", 3.5),
        allow_simultaneous_positions_,
        trend_filter_enabled_);
  }

  // ========================================
  // 100ms interval: Orderbook update
  // ========================================
  void on_orderbook_updated(const common::TickerId& ticker, common::Price,
      common::Side, const MarketOrderBookT* order_book) noexcept {
    ticker_ = ticker;
    uint64_t current_time = get_current_time_ns();

    // === 1. Update orderbook threshold (100ms interval) ===
    dynamic_threshold_->update_orderbook_threshold(order_book);

    // === 2. Calculate final threshold ===
    current_wall_threshold_ =
        dynamic_threshold_->calculate(order_book, current_time);

    // === 3. Detect walls (bidirectional) ===
    bid_wall_info_ = detect_wall(order_book, common::Side::kBuy);
    allow_long_entry_ = bid_wall_info_.is_valid;

    ask_wall_info_ = detect_wall(order_book, common::Side::kSell);
    allow_short_entry_ = ask_wall_info_.is_valid;

    if (log_wall_detection_) {
      this->logger_.info(
          "[Wall@100ms] Threshold:{:.0f} | Bid:{:.0f}@{:.4f}%({}) | "
          "Ask:{:.0f}@{:.4f}%({})",
          current_wall_threshold_,
          bid_wall_info_.accumulated_amount,
          bid_wall_info_.distance_pct * 100,
          bid_wall_info_.is_valid,
          ask_wall_info_.accumulated_amount,
          ask_wall_info_.distance_pct * 100,
          ask_wall_info_.is_valid);
    }

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
      this->logger_.warn("Invalid BBO. Skipping.");
      return;
    }

    // === 1. Update price statistics and trade history ===
    uint64_t current_time = get_current_time_ns();

    // Update Robust Z-score
    robust_zscore_->on_price(market_data->price.value);

    // Update trade history for trend filter
    if (trend_filter_enabled_) {
      recent_trades_.push_back(
          {market_data->side, market_data->qty.value, current_time});
      if (recent_trades_.size() > static_cast<size_t>(trend_lookback_ticks_)) {
        recent_trades_.pop_front();
      }
    }

    // Accumulate trade volume for wall threshold (EMA update)
    dynamic_threshold_->on_trade(current_time,
        market_data->price.value,
        market_data->qty.value);

    // === 2. Long entry check ===
    if (market_data->side == common::Side::kSell && allow_long_entry_ &&
        (!long_position_.is_active || allow_simultaneous_positions_)) {

      if (validate_defense_realtime(market_data,
              prev_bbo_,
              current_bbo,
              common::Side::kBuy)) {
        check_long_entry(market_data, order_book, current_bbo);
      }
    }

    // === 3. Short entry check ===
    if (market_data->side == common::Side::kBuy && allow_short_entry_ &&
        (!short_position_.is_active || allow_simultaneous_positions_)) {

      if (validate_defense_realtime(market_data,
              prev_bbo_,
              current_bbo,
              common::Side::kSell)) {
        check_short_entry(market_data, order_book, current_bbo);
      }
    }

    // === 4. Save BBO snapshot ===
    prev_bbo_ = *current_bbo;
  }

  void on_order_updated(const ExecutionReport*) noexcept {
    // Note: TradeEngine already calls position_keeper_->add_fill(report)
    // Do NOT call it again here to avoid double-counting

    // Get current position from PositionKeeper
    auto* pos_info = this->position_keeper_->get_position_info(ticker_);

    // Sync Long position state
    if (long_position_.is_active && pos_info->long_position_ == 0.0) {
      long_position_.is_active = false;
      if (log_entry_exit_) {
        this->logger_.info("[Exit Complete] Long closed | PnL: {:.2f}",
            pos_info->long_real_pnl_);
      }
    }

    // Sync Short position state
    if (short_position_.is_active && pos_info->short_position_ == 0.0) {
      short_position_.is_active = false;
      if (log_entry_exit_) {
        this->logger_.info("[Exit Complete] Short closed | PnL: {:.2f}",
            pos_info->short_real_pnl_);
      }
    }
  }

 private:
  // ========================================
  // Wall detection (100ms orderbook)
  // ========================================
  WallInfo detect_wall(const MarketOrderBookT* order_book,
      common::Side side) const {
    WallInfo info;
    const auto* bbo = order_book->get_bbo();
    const double base_price = (side == common::Side::kBuy)
                                  ? bbo->bid_price.value
                                  : bbo->ask_price.value;

    double weighted_sum = 0.0;
    std::vector<double> level_qty(wall_max_levels_);
    std::vector<int> level_idx(wall_max_levels_);

    // Peek quantities from orderbook
    int actual_levels = order_book->peek_qty(side == common::Side::kBuy,
        wall_max_levels_,
        level_qty,
        level_idx);

    for (int i = 0; i < actual_levels; ++i) {
      if (level_qty[i] <= 0)
        break;

      // Get price for this level
      double price = base_price;  // Approximate for now
      if (side == common::Side::kBuy) {
        price = base_price - (i * 0.00001);  // Rough estimate
      } else {
        price = base_price + (i * 0.00001);
      }

      double notional = price * level_qty[i];
      info.accumulated_amount += notional;
      weighted_sum += price * notional;
      info.levels_checked = i + 1;

      // Target amount reached
      if (info.accumulated_amount >= current_wall_threshold_) {
        double weighted_avg_price = weighted_sum / info.accumulated_amount;
        info.distance_pct =
            std::abs(weighted_avg_price - base_price) / base_price;
        info.is_valid = (info.distance_pct <= wall_max_distance_pct_);
        break;
      }
    }

    // Target amount not reached (vacuum)
    if (info.accumulated_amount < current_wall_threshold_) {
      info.is_valid = false;
    }

    return info;
  }

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

      if (log_defense_check_) {
        this->logger_.info(
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

      if (log_defense_check_) {
        this->logger_.info(
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
  // Trend acceleration detection
  // ========================================
  bool is_trend_accelerating(common::Side direction) const {
    if (!trend_filter_enabled_)
      return false;
    if (recent_trades_.size() < static_cast<size_t>(trend_lookback_ticks_)) {
      return false;
    }

    // === 1. Direction consistency check ===
    int consecutive_count = 0;
    for (const auto& trade : recent_trades_) {
      if (trade.side == direction) {
        consecutive_count++;
      }
    }

    if (consecutive_count < trend_consecutive_threshold_) {
      return false;  // Direction not strong enough
    }

    // === 2. Volume acceleration check (optional) ===
    if (recent_trades_.size() == static_cast<size_t>(trend_lookback_ticks_)) {
      // Recent 2 ticks average volume
      double vol_recent = (recent_trades_[3].qty + recent_trades_[4].qty) / 2.0;

      // Previous 3 ticks average volume
      double vol_old = (recent_trades_[0].qty + recent_trades_[1].qty +
                           recent_trades_[2].qty) /
                       3.0;

      // Volume accelerating (strong trend signal)
      if (vol_recent > vol_old * trend_volume_multiplier_) {
        return true;  // Strong trend acceleration
      }
    }

    // Direction consistent but volume not accelerating
    // Conservative: still consider it as trend acceleration
    return true;
  }

  // ========================================
  // OBI calculation
  // ========================================
  double calculate_orderbook_imbalance(
      const MarketOrderBookT* order_book) const {
    (void)order_book->peek_qty(true, entry_obi_levels_, bid_qty_, {});
    (void)order_book->peek_qty(false, entry_obi_levels_, ask_qty_, {});

    double bid_total = std::accumulate(bid_qty_.begin(), bid_qty_.end(), 0.0);
    double ask_total = std::accumulate(ask_qty_.begin(), ask_qty_.end(), 0.0);

    // (Bid - Ask) / (Bid + Ask)
    return (bid_total - ask_total) / (bid_total + ask_total + 1e-9);
  }

  // ========================================
  // Long entry
  // ========================================
  void check_long_entry(const MarketData* trade, MarketOrderBookT* order_book,
      const BBO* bbo) {
    // 1. Calculate Robust Z-score
    double z_robust = robust_zscore_->calculate_zscore(trade->price.value);

    if (log_entry_exit_) {
      this->logger_.info(
          "[RobustZ] price:{} | median:{:.2f} | MAD:{:.4f} | z:{:.2f}",
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
      if (log_entry_exit_) {
        this->logger_.info("[Entry Block] Long | No wall | z:{:.2f}", z_robust);
      }
      return;
    }

    // 4. Trend acceleration filter (SAFETY)
    if (is_trend_accelerating(common::Side::kSell)) {
      if (log_entry_exit_) {
        this->logger_.info("[Entry Block] Long | Trend accelerating | z:{:.2f}",
            z_robust);
      }
      return;
    }

    // 5. OBI check (buy dominance)
    double obi = calculate_orderbook_imbalance(order_book);
    if (obi < entry_obi_threshold_)
      return;

    // 6. Spread filter
    double spread =
        (bbo->ask_price.value - bbo->bid_price.value) / bbo->bid_price.value;
    if (spread < min_spread_filter_)
      return;

    // 7. Execute entry
    place_entry_order(common::Side::kBuy, bbo->bid_price.value);

    long_position_.is_active = true;
    long_position_.qty = position_size_;
    long_position_.entry_price = bbo->bid_price.value;
    long_position_.entry_wall_info = bid_wall_info_;

    if (log_entry_exit_) {
      this->logger_.info(
          "[Entry] LONG | z_robust:{:.2f} | price:{} | wall:${:.0f}@{:.4f}% | "
          "obi:{:.2f}",
          z_robust,
          bbo->bid_price.value,
          bid_wall_info_.accumulated_amount,
          bid_wall_info_.distance_pct * 100,
          obi);
    }
  }

  // ========================================
  // Short entry
  // ========================================
  void check_short_entry(const MarketData* trade, MarketOrderBookT* order_book,
      const BBO* bbo) {
    // 1. Calculate Robust Z-score
    double z_robust = robust_zscore_->calculate_zscore(trade->price.value);

    // 2. Check Z-score threshold (overbought)
    if (z_robust <= zscore_entry_threshold_)
      return;

    // 3. Wall existence check (CRITICAL)
    if (!ask_wall_info_.is_valid) {
      if (log_entry_exit_) {
        this->logger_.info("[Entry Block] Short | No wall | z:{:.2f}",
            z_robust);
      }
      return;
    }

    // 4. Trend acceleration filter (SAFETY)
    if (is_trend_accelerating(common::Side::kBuy)) {
      if (log_entry_exit_) {
        this->logger_.info(
            "[Entry Block] Short | Trend accelerating | z:{:.2f}",
            z_robust);
      }
      return;
    }

    // 5. OBI check (sell dominance)
    double obi = calculate_orderbook_imbalance(order_book);
    if (obi > -entry_obi_threshold_)
      return;

    // 6. Spread filter
    double spread =
        (bbo->ask_price.value - bbo->bid_price.value) / bbo->bid_price.value;
    if (spread < min_spread_filter_)
      return;

    // 7. Execute entry
    place_entry_order(common::Side::kSell, bbo->ask_price.value);

    short_position_.is_active = true;
    short_position_.qty = position_size_;
    short_position_.entry_price = bbo->ask_price.value;
    short_position_.entry_wall_info = ask_wall_info_;

    if (log_entry_exit_) {
      this->logger_.info(
          "[Entry] SHORT | z_robust:{:.2f} | price:{} | wall:${:.0f}@{:.4f}% | "
          "obi:{:.2f}",
          z_robust,
          bbo->ask_price.value,
          ask_wall_info_.accumulated_amount,
          ask_wall_info_.distance_pct * 100,
          obi);
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
      intent.price = common::Price{base_price - entry_safety_margin_};
      if constexpr (SelectedOeTraits::supports_position_side()) {
        intent.position_side = common::PositionSide::kLong;
      }
    } else {
      intent.price = common::Price{base_price + entry_safety_margin_};
      if constexpr (SelectedOeTraits::supports_position_side()) {
        intent.position_side = common::PositionSide::kShort;
      }
    }

    intent.qty = Qty{position_size_};
    this->order_manager_->apply({intent});
  }

  // ========================================
  // Position exit monitoring (100ms)
  // ========================================
  void check_position_exit(const MarketOrderBookT* order_book) {
    const auto* bbo = order_book->get_bbo();

    // === Long stop loss ===
    if (long_position_.is_active) {
      bool should_exit = false;
      std::string reason;

      if (!bid_wall_info_.is_valid) {
        should_exit = true;
        reason = "Bid wall vanished";
      } else if (bid_wall_info_.accumulated_amount <
                 long_position_.entry_wall_info.accumulated_amount *
                     wall_amount_decay_ratio_) {
        should_exit = true;
        reason = "Bid wall decayed";
      } else if (bid_wall_info_.distance_pct >
                 long_position_.entry_wall_info.distance_pct *
                     wall_distance_expand_ratio_) {
        should_exit = true;
        reason = "Bid wall expanded";
      } else {
        double loss_pct = (long_position_.entry_price - bbo->bid_price.value) /
                          long_position_.entry_price;
        if (loss_pct > max_loss_pct_) {
          should_exit = true;
          reason = "Max loss reached";
        }
      }

      if (should_exit) {
        emergency_exit(common::Side::kSell, bbo->ask_price.value, reason);
        long_position_.is_active = false;
      }
    }

    // === Short stop loss ===
    if (short_position_.is_active) {
      bool should_exit = false;
      std::string reason;

      if (!ask_wall_info_.is_valid) {
        should_exit = true;
        reason = "Ask wall vanished";
      } else if (ask_wall_info_.accumulated_amount <
                 short_position_.entry_wall_info.accumulated_amount *
                     wall_amount_decay_ratio_) {
        should_exit = true;
        reason = "Ask wall decayed";
      } else if (ask_wall_info_.distance_pct >
                 short_position_.entry_wall_info.distance_pct *
                     wall_distance_expand_ratio_) {
        should_exit = true;
        reason = "Ask wall expanded";
      } else {
        double loss_pct = (bbo->ask_price.value - short_position_.entry_price) /
                          short_position_.entry_price;
        if (loss_pct > max_loss_pct_) {
          should_exit = true;
          reason = "Max loss reached";
        }
      }

      if (should_exit) {
        emergency_exit(common::Side::kBuy, bbo->bid_price.value, reason);
        short_position_.is_active = false;
      }
    }
  }

  // ========================================
  // Emergency exit
  // ========================================
  void emergency_exit(common::Side exit_side, double market_price,
      const std::string& reason) {
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

    this->order_manager_->apply({intent});

    if (log_entry_exit_) {
      this->logger_.warn("[{} Exit] {} | price:{}",
          (exit_side == common::Side::kSell) ? "Long" : "Short",
          reason,
          market_price);
    }
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
  // Config parameters
  const bool allow_simultaneous_positions_;
  const double wall_max_distance_pct_;
  const int wall_max_levels_;
  const double defense_qty_multiplier_;
  const double entry_obi_threshold_;
  const int entry_obi_levels_;
  const double position_size_;
  const double entry_safety_margin_;
  const double min_spread_filter_;
  const double wall_amount_decay_ratio_;
  const double wall_distance_expand_ratio_;
  const double max_loss_pct_;
  const bool log_wall_detection_;
  const bool log_defense_check_;
  const bool log_entry_exit_;
  const int zscore_window_size_;
  const int zscore_min_samples_;
  const double zscore_entry_threshold_;
  const bool trend_filter_enabled_;
  const int trend_lookback_ticks_;
  const int trend_consecutive_threshold_;
  const double trend_volume_multiplier_;

  // Dynamic state
  common::TickerId ticker_;
  WallInfo bid_wall_info_;
  WallInfo ask_wall_info_;
  bool allow_long_entry_{false};
  bool allow_short_entry_{false};
  PositionState long_position_;
  PositionState short_position_;
  BBO prev_bbo_;

  // OBI calculation buffers
  mutable std::vector<double> bid_qty_;
  mutable std::vector<double> ask_qty_;

  // Dynamic threshold
  double current_wall_threshold_;
  std::unique_ptr<DynamicWallThreshold> dynamic_threshold_;

  // Robust Z-score module
  std::unique_ptr<RobustZScore> robust_zscore_;

  // Trade history for trend detection
  std::deque<TradeRecord> recent_trades_;
};

}  // namespace trading

#endif  // MEAN_REVERSION_MAKER_H
