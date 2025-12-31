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

#ifndef DIRECTIONAL_STRATEGY_H
#define DIRECTIONAL_STRATEGY_H
#include "../base_strategy.hpp"
#include "common/ini_config.hpp"
#include "feature_engine.hpp"
#include "oe_traits_config.hpp"
#include "order_book.hpp"
#include "order_manager.hpp"

struct MarketData;

namespace trading {
template <typename Strategy>
class FeatureEngine;
template <typename Strategy>
class MarketOrderBook;

class ObiVwapDirectionalStrategy
    : public BaseStrategy<ObiVwapDirectionalStrategy> {
 public:
  using QuoteIntentType =
      std::conditional_t<SelectedOeTraits::supports_position_side(),
          FuturesQuoteIntent, SpotQuoteIntent>;
  using OrderManagerT = OrderManager<ObiVwapDirectionalStrategy>;
  using FeatureEngineT = FeatureEngine<ObiVwapDirectionalStrategy>;
  using MarketOrderBookT = MarketOrderBook<ObiVwapDirectionalStrategy>;
  double round5(double value) {
    constexpr double kFactor = 100000.0;
    return std::round(value * kFactor) / kFactor;
  }

  ObiVwapDirectionalStrategy(OrderManagerT* order_manager,
      const FeatureEngineT* feature_engine,
      const InventoryManager* inventory_manager,
      PositionKeeper* position_keeper, const common::Logger::Producer& logger,
      const common::TradeEngineCfgHashMap&)
      : BaseStrategy(order_manager, feature_engine, inventory_manager,
            position_keeper, logger),
        obi_level_(
            INI_CONFIG.get_int("strategy", "obi_level", kDefaultOBILevel10)),
        safety_margin_(static_cast<int64_t>(
            INI_CONFIG.get_double("strategy", "safety_margin") *
            common::FixedPointConfig::kPriceScale)),
        minimum_spread_(INI_CONFIG.get_int64("strategy", "minimum_spread")),
        enter_threshold_(INI_CONFIG.get_int64("strategy", "enter_threshold")),
        exit_threshold_(INI_CONFIG.get_int64("strategy", "exit_threshold")),
        position_variance_(
            INI_CONFIG.get_int64("strategy", "position_variance")),
        bid_qty_(obi_level_),
        ask_qty_(obi_level_) {
    assert(obi_level_ > 0 && ((obi_level_ & (obi_level_ - 1)) == 0));
  }

  void on_orderbook_updated(const common::TickerId&, common::PriceType,
      common::Side, const MarketOrderBookT*) noexcept {}

  void on_trade_updated(const MarketData* market_data,
      MarketOrderBookT* order_book) noexcept {
    const auto ticker = market_data->ticker_id;
    const auto* bbo = order_book->get_bbo();

    if (!bbo->bid_qty.is_valid() || !bbo->ask_qty.is_valid() ||
        !bbo->bid_price.is_valid() || !bbo->ask_price.is_valid() ||
        bbo->ask_price < bbo->bid_price) {
      this->logger_.warn("Invalid BBO. Skipping quoting.");
      return;
    }

    std::ignore = order_book->peek_qty(true,
        obi_level_,
        std::span<int64_t>(bid_qty_),
        {});
    std::ignore = order_book->peek_qty(false,
        obi_level_,
        std::span<int64_t>(ask_qty_),
        {});

    const int64_t vwap = this->feature_engine_->get_vwap();
    const int64_t obi = this->feature_engine_->orderbook_imbalance_int64(bid_qty_, ask_qty_);
    const int64_t mid = this->feature_engine_->get_mid_price();
    const int64_t spread = this->feature_engine_->get_spread_fast();
    const int64_t denom = std::max(spread, minimum_spread_);
    constexpr int64_t kDeltaScale = 100;
    const int64_t delta_scaled =
        (denom > 0) ? ((mid - vwap) * kDeltaScale) / denom : 0;
    const int64_t delta_obi_scaled = delta_scaled * obi;
    const int64_t signal = std::abs(delta_obi_scaled);

    std::vector<QuoteIntentType> intents;
    intents.reserve(1);

    this->logger_.debug(
        "[Updated] delta:{} obi:{} signal:{} mid:{}, vwap:{}, spread:{}",
        delta_obi_scaled,
        obi,
        signal,
        mid,
        vwap,
        spread);

    const int64_t delta_obi = delta_obi_scaled;

    if (delta_obi > enter_threshold_) {
      const int64_t best_bid_price = bbo->bid_price.value;
      const auto order_price =
          common::PriceType::from_raw(best_bid_price - safety_margin_);
      const auto order_qty =
          common::QtyType::from_raw(signal / position_variance_);
      auto intent =
          make_quote_intent(ticker, common::Side::kBuy, order_price, order_qty);
      if constexpr (SelectedOeTraits::supports_position_side()) {
        intent.position_side = common::PositionSide::kLong;
      }
      intents.push_back(intent);

      this->logger_.debug("[Directional]Long Entry. price:{}, qty:{}, side:buy",
          order_price.value,
          order_qty.value);
    } else if (delta_obi < -enter_threshold_) {
      const int64_t best_ask_price = bbo->ask_price.value;
      const auto order_price =
          common::PriceType::from_raw(best_ask_price + safety_margin_);
      const auto order_qty =
          common::QtyType::from_raw(signal / position_variance_);
      auto intent = make_quote_intent(ticker,
          common::Side::kSell,
          order_price,
          order_qty);
      if constexpr (SelectedOeTraits::supports_position_side()) {
        intent.position_side = common::PositionSide::kShort;
      }
      intents.push_back(intent);
      this->logger_.debug(
          "[Directional]Short Entry. price:{}, qty:{}, side:sell",
          order_price.value,
          order_qty.value);
    } else if (delta_obi < exit_threshold_) {
      if constexpr (SelectedOeTraits::supports_position_side()) {
        const auto* pos_info =
            this->position_keeper_->get_position_info(ticker);
        if (pos_info->long_position_raw_ > 0) {
          const int64_t best_bid_price = bbo->bid_price.value;
          const auto order_price =
              common::PriceType::from_raw(best_bid_price + safety_margin_);
          const auto order_qty =
              common::QtyType::from_raw(signal / position_variance_);
          auto intent = make_quote_intent(ticker,
              common::Side::kSell,
              order_price,
              order_qty);
          intent.position_side = common::PositionSide::kLong;
          intents.push_back(intent);
          this->logger_.debug(
              "[Directional]Long Exit. price:{}, qty:{}, side:sell",
              order_price.value,
              order_qty.value);
        }
      }
    } else if (delta_obi > -exit_threshold_) {
      if constexpr (SelectedOeTraits::supports_position_side()) {
        const auto* pos_info =
            this->position_keeper_->get_position_info(ticker);
        if (pos_info->short_position_raw_ > 0) {
          const int64_t best_ask_price = bbo->ask_price.value;
          const auto order_price =
              common::PriceType::from_raw(best_ask_price - safety_margin_);
          const auto order_qty =
              common::QtyType::from_raw(signal / position_variance_);
          auto intent = make_quote_intent(ticker,
              common::Side::kBuy,
              order_price,
              order_qty);
          intent.position_side = common::PositionSide::kShort;
          intents.push_back(intent);
          this->logger_.debug(
              "[Directional]Short Exit. price:{}, qty:{}, side:buy",
              order_price.value,
              order_qty.value);
        }
      }
    }

    this->order_manager_->apply(intents);
  }

  void on_order_updated(const ExecutionReport*) noexcept {}

 private:
  static QuoteIntentType make_quote_intent(const common::TickerId& ticker,
      common::Side side, common::PriceType price, common::QtyType qty) {
    QuoteIntentType intent{};
    intent.ticker = ticker;
    intent.side = side;
    intent.price = price;
    intent.qty = qty;
    return intent;
  }

  static constexpr int kDefaultOBILevel10 = 10;
  const int obi_level_;

  const int64_t safety_margin_;
  const int64_t minimum_spread_;
  const int64_t enter_threshold_;
  const int64_t exit_threshold_;
  const int64_t position_variance_;
  std::vector<int64_t> bid_qty_;
  std::vector<int64_t> ask_qty_;
};

}  // namespace trading

#endif  //DIRECTIONAL_STRATEGY_H
