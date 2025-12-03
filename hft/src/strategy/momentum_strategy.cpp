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

#include "momentum_strategy.h"
#include "common/ini_config.hpp"
#include "feature_engine.h"
#include "order_book.h"
#include "order_manager.h"

using common::Qty;
using common::Side;
inline double round5(double value) {
  constexpr double kFactor = 100000.0;
  constexpr double kInvFactor = 1.0 / kFactor;
  return std::round(value * kFactor) * kInvFactor;
}

namespace trading {

template <typename App>
ObiVwapMomentumStrategyTemplate<App>::ObiVwapMomentumStrategyTemplate(
    OrderManagerT* const order_manager,
    const FeatureEngineT* const feature_engine, common::Logger* logger,
    const common::TradeEngineCfgHashMap&)
    : Base(order_manager, feature_engine, logger),
      variance_denominator_(
          INI_CONFIG.get_double("strategy", "variance_denominator")),
      position_variance_(
          INI_CONFIG.get_double("strategy", "position_variance") /
          variance_denominator_),
      enter_threshold_(INI_CONFIG.get_double("strategy", "enter_threshold")),
      exit_threshold_(INI_CONFIG.get_double("strategy", "exit_threshold")),
      obi_level_(
          INI_CONFIG.get_int("strategy", "obi_level", kDefaultOBILevel10)),
      bid_qty_(obi_level_),
      ask_qty_(obi_level_) {}

template <typename App>
void ObiVwapMomentumStrategyTemplate<App>::on_orderbook_updated(
    const common::TickerId&, common::Price, Side,
    const MarketOrderBookT*) noexcept {}

template <typename App>
void ObiVwapMomentumStrategyTemplate<App>::on_trade_updated(
    const MarketData* market_data, MarketOrderBookT* order_book) noexcept {
  const auto ticker = market_data->ticker_id;
  const auto* bbo = order_book->get_bbo();
  if (bbo->bid_qty.value == common::kQtyInvalid ||
      bbo->ask_qty.value == common::kQtyInvalid ||
      bbo->bid_price.value == common::kPriceInvalid ||
      bbo->ask_price.value == common::kPriceInvalid ||
      bbo->ask_price.value < bbo->bid_price.value) {
    this->logger_.trace("Invalid BBO. Skipping quoting.");
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
  const double denom = std::max({spread, 0.01});
  const auto delta = (mid - vwap) / denom;
  if (!std::isfinite(spread) || spread <= 0.0) {
    this->logger_.trace("Non-positive spread ({}). Using denom={}",
        spread,
        denom);
  }
  const auto signal = std::abs(delta * obi);

  std::vector<QuoteIntent> intents;
  intents.reserve(4);

  this->logger_.trace(
      "[Updated] delta:{} obi:{} signal:{} mid:{}, vwap:{}, spread:{}",
      delta,
      obi,
      signal,
      mid,
      vwap,
      spread);

  if (delta * obi > enter_threshold_) {
    const auto best_bid_price = order_book->get_bbo()->bid_price;
    intents.push_back(QuoteIntent{.ticker = ticker,
        .side = Side::kBuy,
        .price = best_bid_price - kSafetyMargin,
        .qty = Qty{round5(signal * position_variance_)}});

    this->logger_.trace(
        "[MarketMaker]Order Submitted. price:{}, qty:{}, side:buy, delta:{} "
        "obi:{} signal:{} "
        "mid:{}, "
        "vwap:{}, spread:{}",
        best_bid_price.value - kSafetyMargin,
        round5(signal * position_variance_),
        delta,
        obi,
        signal,
        mid,
        vwap,
        spread);
  } else if (delta * obi < -enter_threshold_) {
    const auto best_ask_price = order_book->get_bbo()->ask_price;
    intents.push_back(QuoteIntent{.ticker = ticker,
        .side = Side::kSell,
        .price = best_ask_price + kSafetyMargin,
        .qty = Qty{round5(signal * position_variance_)}});
    this->logger_.trace(
        "[MarketMaker]Order Submitted. price:{}, qty:{}, side:sell, delta:{} "
        "obi:{} signal:{} "
        "mid:{}, "
        "vwap:{}, spread:{}",
        best_ask_price.value + kSafetyMargin,
        round5(signal * position_variance_),
        delta,
        obi,
        signal,
        mid,
        vwap,
        spread);
  }
  if (signal < exit_threshold_) {
    return;
  }

  this->order_manager_->apply(intents);
}

template <typename App>
void ObiVwapMomentumStrategyTemplate<App>::on_order_updated(
    const ExecutionReport*) noexcept {}

#ifdef ENABLE_WEBSOCKET
template class ObiVwapMomentumStrategyTemplate<core::WsOrderEntryApp>;
#else
template class ObiVwapMomentumStrategyTemplate<core::FixOrderEntryApp>;
#endif

}  // namespace trading
