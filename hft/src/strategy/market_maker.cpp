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

#include "market_maker.h"
#include "feature_engine.h"
#include "ini_config.hpp"
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
MarketMaker::MarketMaker(OrderManager<MarketMaker>* const order_manager,
                         const FeatureEngine<MarketMaker>* const feature_engine,
                         const InventoryManager* const inventory_manager,
                         common::Logger* logger,
                         const common::TradeEngineCfgHashMap&)
    : BaseStrategy(order_manager, feature_engine, inventory_manager, logger),
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

void MarketMaker::on_orderbook_updated(
    const common::TickerId&, common::Price, Side,
    const MarketOrderBook<MarketMaker>*) noexcept {}

void MarketMaker::on_trade_updated(
    const MarketData* market_data,
    MarketOrderBook<MarketMaker>* order_book) noexcept {
  const auto ticker = market_data->ticker_id;
  const auto* bbo = order_book->get_bbo();
  if (bbo->bid_qty.value == common::kQtyInvalid ||
      bbo->ask_qty.value == common::kQtyInvalid ||
      bbo->bid_price.value == common::kPriceInvalid ||
      bbo->ask_price.value == common::kPriceInvalid ||
      bbo->ask_price.value < bbo->bid_price.value) {
    logger_.debug("Invalid BBO. Skipping quoting.");
    return;
  }

  (void)order_book->peek_qty(true, obi_level_, bid_qty_, {});
  (void)order_book->peek_qty(false, obi_level_, ask_qty_, {});

  const auto vwap = feature_engine_->get_vwap();
  const auto spread = feature_engine_->get_spread();

  const double obi =
      FeatureEngine<MarketMaker>::orderbook_imbalance_from_levels(bid_qty_,
                                                                  ask_qty_);
  const auto mid = (order_book->get_bbo()->bid_price.value +
                    order_book->get_bbo()->ask_price.value) *
                   0.5;
  const double denom = std::max({spread, 0.01});
  const auto delta = (mid - vwap) / denom;
  if (!std::isfinite(spread) || spread <= 0.0) {
    logger_.trace(
        std::format("Non-positive spread ({}). Using denom={}", spread, denom));
  }
  const auto signal = std::abs(delta * obi);

  std::vector<QuoteIntent> intents;
  intents.reserve(4);

  logger_.debug(std::format(
      "[Updated] delta:{} obi:{} signal:{} mid:{}, vwap:{}, spread:{}", delta,
      obi, signal, mid, vwap, spread));

  if (delta * obi > enter_threshold_) {
    const auto best_bid_price = order_book->get_bbo()->bid_price;
    const auto bid_adjustment =
        inventory_manager_->get_quote_adjustment(Side::kBuy, ticker);
    const auto adjusted_bid_price =
        best_bid_price.value - kGap + bid_adjustment;

    intents.push_back(
        QuoteIntent{.ticker = ticker,
                    .side = Side::kBuy,
                    .price = common::Price{adjusted_bid_price},
                    .qty = Qty{round5(signal * position_variance_)}});

    logger_.debug(std::format(
        "[MarketMaker]Order Submitted. price:{}, qty:{}, side:buy, delta:{} "
        "obi:{} signal:{} "
        "mid:{}, "
        "vwap:{}, spread:{}, inventory_adj:{}",
        adjusted_bid_price, round5(signal * position_variance_), delta, obi,
        signal, mid, vwap, spread, bid_adjustment));
  } else if (delta * obi < -enter_threshold_) {
    const auto best_ask_price = order_book->get_bbo()->ask_price;
    const auto ask_adjustment =
        inventory_manager_->get_quote_adjustment(Side::kSell, ticker);
    const auto adjusted_ask_price =
        best_ask_price.value + kGap + ask_adjustment;

    intents.push_back(
        QuoteIntent{.ticker = ticker,
                    .side = Side::kSell,
                    .price = common::Price{adjusted_ask_price},
                    .qty = Qty{round5(signal * position_variance_)}});
    logger_.debug(std::format(
        "[MarketMaker]Order Submitted. price:{}, qty:{}, side:sell, delta:{} "
        "obi:{} signal:{} "
        "mid:{}, "
        "vwap:{}, spread:{}, inventory_adj:{}",
        adjusted_ask_price, round5(signal * position_variance_), delta, obi,
        signal, mid, vwap, spread, ask_adjustment));
  }
  if (signal < exit_threshold_) {
    return;
  }

  order_manager_->apply(intents);
}

void MarketMaker::on_order_updated(const ExecutionReport*) noexcept {}

}  // namespace trading
