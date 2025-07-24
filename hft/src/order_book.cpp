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

#include "order_book.h"

#include "market_data.h"
#include "trade_engine.h"

using common::MarketUpdateType;
using common::Price;
using common::Qty;
using common::Side;
using common::TickerId;

namespace trading {
MarketOrder::MarketOrder() = default;

MarketOrder::MarketOrder(const Qty qty_, const bool active_ = false) noexcept
    : qty(qty_), active(active_) {}

auto MarketOrder::toString() const -> std::string {
  std::stringstream stream;
  stream << "[MarketOrder]" << "[" << "qty:" << qty << " "
         << "active:" << active << " ";

  return stream.str();
}

MarketOrderBook::MarketOrderBook(const TickerId& ticker_id,
                                 common::Logger* logger)
    : ticker_id_(std::move(ticker_id)), logger_(logger) {}

MarketOrderBook::~MarketOrderBook() {
  logger_->info("MarketOrderBook::~MarketOrderBook");

  trade_engine_ = nullptr;
  logger_ = nullptr;
}

void MarketOrderBook::add_order(const MarketData* market_update) {
  const auto index = priceToIndex(market_update->price);
  const bool is_bid = market_update->side == Side::kBuy;
  auto& side = is_bid ? bids_ : asks_;

  // Do not check active state because, hidden order exist in edge depth.
  side[index].active = true;
  side[index].qty = market_update->qty;

  activate_level(is_bid, index);
}

void MarketOrderBook::modify_order(const MarketData* market_update) {
  const auto index = priceToIndex(market_update->price);
  const bool is_bid = market_update->side == Side::kBuy;
  auto& side = is_bid ? bids_ : asks_;

  // Do not check active state because, hidden order exist in edge depth.
  side[index].active = true;
  side[index].qty = market_update->qty;

  activate_level(is_bid, index);
}

void MarketOrderBook::delete_order(const MarketData* market_update) {
  const auto index = priceToIndex(market_update->price);
  const bool is_bid = market_update->side == Side::kBuy;
  auto& side = is_bid ? bids_ : asks_;

  if (UNLIKELY(!side[index].active)) {
    logger_->info(std::format("bids_[index] doest not active. price: {}",
                              market_update->price));
    return;
  }
  side[index].qty = 0;
  side[index].active = false;

  clear_level(is_bid, index);
}

void MarketOrderBook::trade_order(const MarketData* market_update) {
  const auto index = priceToIndex(market_update->price);
  auto& side = market_update->side == Side::kBuy ? bids_ : asks_;
  side[index].qty -= market_update->qty;
  side[index].active = side[index].qty > 0;
}

/// Process market data update and update the limit order book.
auto MarketOrderBook::on_market_data_updated(
    const MarketData* market_update) noexcept -> void {
  switch (market_update->type) {
    case MarketUpdateType::kAdd: {
      add_order(market_update);
    } break;
    case MarketUpdateType::kModify: {
      modify_order(market_update);
    } break;
    case MarketUpdateType::kCancel: {
      delete_order(market_update);
    } break;
    case MarketUpdateType::kTrade: {
      //Do I need to update local orderbook?
      trade_order(market_update);
      trade_engine_->on_trade_updated(market_update, this);
      return;
    } break;
    case MarketUpdateType::kClear: {
      // TODO(jb): check if memset is possible
      const MarketOrder default_order{.0f, false};
      bids_.fill(default_order);
      asks_.fill(default_order);
    } break;
    case MarketUpdateType::kInvalid:
      logger_->error("error in market update data");
      break;
  }

  logger_->debug(std::format("{}:{} {}() {} {}\n", __FILE__, __LINE__,
                             __FUNCTION__, market_update->toString(),
                             bbo_.toString()));

  trade_engine_->on_order_book_updated(market_update->price,
                                       market_update->side, this);
}

auto MarketOrderBook::get_bbo() noexcept -> const BBO* {
  bbo_.ask_price = best_ask_price();
  bbo_.ask_qty =
      best_ask_idx() == -1 ? common::kQtyInvalid : asks_[best_ask_idx()].qty;
  bbo_.bid_price = best_bid_price();
  bbo_.bid_qty =
      best_bid_idx() == -1 ? common::kQtyInvalid : bids_[best_bid_idx()].qty;
  return &bbo_;
}

void MarketOrderBook::on_trade_update(MarketData*) {}

}  // namespace trading