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

constexpr double kThreshold = 1.;
constexpr double kPositionVariance = 1.6;

namespace trading {
MarketMaker::MarketMaker(OrderManager* const order_manager,
                         const FeatureEngine* const feature_engine,
                         common::Logger* logger,
                         const common::TradeEngineCfgHashMap&)
    : BaseStrategy(order_manager, feature_engine, logger) {}

void MarketMaker::on_orderbook_updated(
    const common::TickerId& ticker_id, common::Price, common::Side,
    const MarketOrderBook* order_book) noexcept {

  std::vector<double> bid_qty(kVwapSize);
  std::vector<double> ask_qty(kVwapSize);

  [[maybe_unused]] const int abc =
      order_book->peek_qty(true, kVwapSize, bid_qty, {});
  [[maybe_unused]] const int bcd =
      order_book->peek_qty(false, kVwapSize, ask_qty, {});

  const auto best_bid_price = order_book->get_bbo()->bid_price;
  const auto vwap = feature_engine_->get_vwap();
  const auto spread = feature_engine_->get_spread();

  const double obi =
      FeatureEngine::orderbook_imbalance_from_levels(bid_qty, ask_qty);
  const auto mid = (order_book->get_bbo()->bid_price.value +
                    order_book->get_bbo()->ask_price.value) *
                   0.5;
  const auto delta = (mid - vwap) / spread;
  if (delta * obi > kThreshold) {
    //TODO(JB): Implement qty clip using ticker_cfg
    order_manager_->move_order(ticker_id, best_bid_price, common::Side::kBuy,
                               common::Qty{delta * obi * kPositionVariance});
    logger_->info(std::format("Order Created! price:{}, qty:{}",
                              best_bid_price.value,
                              delta * obi * kPositionVariance));
  }
}

void MarketMaker::on_trade_updated(const MarketData*,
                                   MarketOrderBook*) noexcept {}

void MarketMaker::on_order_updated(const ExecutionReport*) noexcept {}
}  // namespace trading