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
#include "order_book.h"
#include "order_manager.h"

constexpr double kPositionVariance = 1.6;
constexpr double kEnterThreshold = 1.;
constexpr double kExitThreshold = 0.8;
using common::Qty;
using common::Side;

namespace trading {
MarketMaker::MarketMaker(OrderManager* const order_manager,
                         const FeatureEngine* const feature_engine,
                         common::Logger* logger,
                         const common::TradeEngineCfgHashMap&)
    : BaseStrategy(order_manager, feature_engine, logger) {}

void MarketMaker::on_orderbook_updated(
    const common::TickerId& ticker, common::Price, common::Side,
    const MarketOrderBook* order_book) noexcept {

  std::vector<double> bid_qty(kVwapSize);
  std::vector<double> ask_qty(kVwapSize);

  (void)order_book->peek_qty(true, kVwapSize, bid_qty, {});
  (void)order_book->peek_qty(false, kVwapSize, ask_qty, {});

  const auto vwap = feature_engine_->get_vwap();
  const auto spread = feature_engine_->get_spread();

  const double obi =
      FeatureEngine::orderbook_imbalance_from_levels(bid_qty, ask_qty);
  const auto mid = (order_book->get_bbo()->bid_price.value +
                    order_book->get_bbo()->ask_price.value) *
                   0.5;
  const auto delta = (mid - vwap) / spread;
  const auto signal = delta * obi;

  std::vector<QuoteIntent> intents;
  intents.reserve(4);

  if (delta * obi > kEnterThreshold) {
    //TODO(JB): Implement qty clip using ticker_cfg
    const auto best_bid_price = order_book->get_bbo()->bid_price;
    intents.push_back(QuoteIntent{ticker, Side::kBuy, best_bid_price,
                                  Qty{delta * obi * kPositionVariance}});

    logger_->info(std::format("Order Created! price:{}, qty:{}",
                              best_bid_price.value,
                              delta * obi * kPositionVariance));
  } else if (delta * obi < -kEnterThreshold) {
    const auto best_ask_price = order_book->get_bbo()->ask_price;
    intents.push_back(QuoteIntent{ticker, Side::kSell, best_ask_price,
                                  Qty{delta * obi * kPositionVariance}});
    logger_->info(std::format("Order Created! price:{}, qty:{}",
                              best_ask_price.value,
                              delta * obi * kPositionVariance));
  }
  if (std::abs(signal) < kExitThreshold) {
    intents.clear();
  }
  order_manager_->apply(intents);
}

void MarketMaker::on_trade_updated(const MarketData*,
                                   MarketOrderBook*) noexcept {}

void MarketMaker::on_order_updated(const ExecutionReport*) noexcept {}
}  // namespace trading