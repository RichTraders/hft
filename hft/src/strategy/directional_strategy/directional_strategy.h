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

class ObiVwapDirectionalStrategy : public BaseStrategy<ObiVwapDirectionalStrategy> {
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
        variance_denominator_(
            INI_CONFIG.get_double("strategy", "variance_denominator")),
        position_variance_(
            INI_CONFIG.get_double("strategy", "position_variance") /
            variance_denominator_),
        enter_threshold_(INI_CONFIG.get_double("strategy", "enter_threshold")),
        exit_threshold_(INI_CONFIG.get_double("strategy", "exit_threshold")),
        obi_level_(
            INI_CONFIG.get_int("strategy", "obi_level", kDefaultOBILevel10)),
        safety_margin_(INI_CONFIG.get_double("strategy", "safety_margin",
            kDefaultSafetyMargin)),
        minimum_spread_(
            1 / std::pow(kDenominatorBase, PRECISION_CONFIG.price_precision())),
        bid_qty_(obi_level_),
        ask_qty_(obi_level_) {}

  void on_orderbook_updated(const common::TickerId&, common::Price,
      common::Side, const MarketOrderBookT*) noexcept {}

  void on_trade_updated(const MarketData* market_data,
      MarketOrderBookT* order_book) noexcept {
    const auto ticker = market_data->ticker_id;
    const auto* bbo = order_book->get_bbo();
    if (bbo->bid_qty.value == common::kQtyInvalid ||
        bbo->ask_qty.value == common::kQtyInvalid ||
        bbo->bid_price.value == common::kPriceInvalid ||
        bbo->ask_price.value == common::kPriceInvalid ||
        bbo->ask_price.value < bbo->bid_price.value) {
      this->logger_.warn("Invalid BBO. Skipping quoting.");
      return;
    }

    (void)order_book->peek_qty(true, obi_level_, bid_qty_, {});
    (void)order_book->peek_qty(false, obi_level_, ask_qty_, {});

    const auto vwap = this->feature_engine_->get_vwap();
    const auto spread = this->feature_engine_->get_spread();

    const double obi =
        FeatureEngineT::orderbook_imbalance_from_levels(bid_qty_, ask_qty_);
    const auto mid = (order_book->get_bbo()->bid_price.value +
                         order_book->get_bbo()->ask_price.value) *
                     0.5;
    const double denom = std::max({spread, minimum_spread_});
    const auto delta = (mid - vwap) / denom;
    if (!std::isfinite(spread) || spread <= 0.0) {
      this->logger_.trace("Non-positive spread ({}). Using denom={}",
          spread,
          denom);
    }
    const auto signal = std::abs(delta * obi);

    std::vector<QuoteIntentType> intents;
    intents.reserve(4);

    this->logger_.debug(
        "[Updated] delta:{} obi:{} signal:{} mid:{}, vwap:{}, spread:{:.4f}",
        delta,
        obi,
        signal,
        mid,
        vwap,
        spread);

    if (delta * obi > enter_threshold_) {
      const auto best_bid_price = order_book->get_bbo()->bid_price;
      auto intent = make_quote_intent(ticker,
          common::Side::kBuy,
          best_bid_price - safety_margin_,
          Qty{round5(signal * position_variance_)});
      if constexpr (SelectedOeTraits::supports_position_side()) {
        intent.position_side = common::PositionSide::kLong;
      }
      intents.push_back(intent);

      this->logger_.debug(
          "[Directional]Long Entry. price:{}, qty:{}, side:buy, "
          "delta:{} "
          "obi:{} signal:{}, mid:{}, vwap:{}, spread:{:.4f}",
          best_bid_price.value - safety_margin_,
          round5(signal * position_variance_),
          delta,
          obi,
          signal,
          mid,
          vwap,
          spread);
    } else if (delta * obi < -enter_threshold_) {
      const auto best_ask_price = order_book->get_bbo()->ask_price;
      auto intent = make_quote_intent(ticker,
          common::Side::kSell,
          best_ask_price + safety_margin_,
          Qty{round5(signal * position_variance_)});
      if constexpr (SelectedOeTraits::supports_position_side()) {
        intent.position_side = common::PositionSide::kShort;
      }
      intents.push_back(intent);
      this->logger_.debug(
          "[Directional]Short Entry. price:{}, qty:{}, side:sell, "
          "delta:{} "
          "obi:{} signal:{}, mid:{}, vwap:{}, spread:{:.4f}",
          best_ask_price.value + safety_margin_,
          round5(signal * position_variance_),
          delta,
          obi,
          signal,
          mid,
          vwap,
          spread);
    } else if (delta * obi < exit_threshold_) {
      if constexpr (SelectedOeTraits::supports_position_side()) {
        const auto* pos_info =
            this->position_keeper_->get_position_info(ticker);
        if (pos_info->long_position_ > 0) {
          const auto best_bid_price = order_book->get_bbo()->bid_price;
          auto intent = make_quote_intent(ticker,
              common::Side::kSell,
              best_bid_price + safety_margin_,
              Qty{round5(signal * position_variance_)});
          intent.position_side = common::PositionSide::kLong;
          intents.push_back(intent);
          this->logger_.debug(
              "[Directional]Long Exit. price:{}, qty:{}, side:sell, "
              "delta:{} "
              "obi:{} signal:{}, mid:{}, vwap:{}, spread:{:.4f}",
              best_bid_price.value - safety_margin_,
              round5(signal * position_variance_),
              delta,
              obi,
              signal,
              mid,
              vwap,
              spread);
        }
      }
    } else if (delta * obi > -exit_threshold_) {
      if constexpr (SelectedOeTraits::supports_position_side()) {
        const auto* pos_info =
            this->position_keeper_->get_position_info(ticker);
        if (pos_info->short_position_ > 0) {
          const auto best_ask_price = order_book->get_bbo()->ask_price;
          auto intent = make_quote_intent(ticker,
              common::Side::kBuy,
              best_ask_price - safety_margin_,
              Qty{round5(signal * position_variance_)});
          intent.position_side = common::PositionSide::kShort;
          intents.push_back(intent);
          this->logger_.debug(
              "[Directional]Short Exit. price:{}, qty:{}, side:buy, "
              "delta:{} "
              "obi:{} signal:{}, mid:{}, vwap:{}, spread:{:.4f}",
              best_ask_price.value + safety_margin_,
              round5(signal * position_variance_),
              delta,
              obi,
              signal,
              mid,
              vwap,
              spread);
        }
      }
    }

    this->order_manager_->apply(intents);
  }

  void on_order_updated(const ExecutionReport*) noexcept {}

 private:
  static QuoteIntentType make_quote_intent(const common::TickerId& ticker,
      common::Side side, common::Price price, Qty qty) {
    QuoteIntentType intent{};
    intent.ticker = ticker;
    intent.side = side;
    intent.price = price;
    intent.qty = qty;
    return intent;
  }

  static constexpr int kDefaultOBILevel10 = 10;
  static constexpr double kDefaultSafetyMargin = 5.0;
  static constexpr int kDenominatorBase = 10;
  const double variance_denominator_;
  const double position_variance_;
  const double enter_threshold_;
  const double exit_threshold_;
  const int obi_level_;
  const double safety_margin_;
  const double minimum_spread_;
  std::vector<double> bid_qty_;
  std::vector<double> ask_qty_;
};

}  // namespace trading

#endif  //DIRECTIONAL_STRATEGY_H
