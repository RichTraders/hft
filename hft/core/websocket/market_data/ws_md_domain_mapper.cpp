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

#include "ws_md_domain_mapper.h"

#include "common/ini_config.hpp"
#include "common/logger.h"
#include "common/memory_pool.hpp"
#include "core/market_data.h"

#include "common/types.h"

namespace core {
using common::MarketUpdateType;
using common::OrderId;
using common::Price;
using common::Qty;
using common::TickerId;

using schema::ApiResponse;
using schema::DepthResponse;
using schema::DepthSnapshot;
using schema::ExchangeInfoResponse;
using schema::TradeEvent;

MarketUpdateData WsMdDomainMapper::to_market_data(
    const WireMessage& msg) const {
  if (const auto* depth = as_depth(msg)) {
    return build_depth_update(*depth, MarketDataType::kMarket);
  }
  if (const auto* trade = as_trade(msg)) {
    return build_trade_update(*trade);
  }
  return MarketUpdateData{MarketDataType::kNone, {}};
}

MarketUpdateData WsMdDomainMapper::to_snapshot_data(
    const WireMessage& msg) const {
  if (const auto* snapshot = as_depth_snapshot(msg)) {
    return build_depth_snapshot(*snapshot, MarketDataType::kMarket);
  }
  logger_.error("Snapshot requested from non-depth wire message");
  return MarketUpdateData{MarketDataType::kNone, {}};
}

InstrumentInfo WsMdDomainMapper::to_instrument_info(
    const WireMessage& msg) const {
  const auto* response = as_exchange_info(msg);

  InstrumentInfo info;
  info.instrument_req_id = response->id;
  const auto& symbols = response->result.symbols;
  info.no_related_sym = static_cast<int>(symbols.size());
  info.symbols.reserve(symbols.size());

  auto parse_or_default = [](const std::optional<std::string>& str,
                              double default_value = 0.0) -> double {
    if (!str || str->empty())
      return default_value;
    const char* begin = str->c_str();
    char* end = nullptr;
    const double data = std::strtod(begin, &end);
    if (end == begin) {
      return default_value;
    }
    return data;
  };

  for (const auto& sym : symbols) {
    InstrumentInfo::RelatedSymT related{};

    related.symbol = sym.symbol;
    related.currency = sym.quote_asset;

    const schema::SymbolFilter* lot_filter = nullptr;
    const schema::SymbolFilter* mlot_filter = nullptr;
    const schema::SymbolFilter* price_filter = nullptr;

    for (const auto& filter : sym.filters) {
      if (filter.filter_type == "LOT_SIZE") {
        lot_filter = &filter;
      } else if (filter.filter_type == "MARKET_LOT_SIZE") {
        mlot_filter = &filter;
      } else if (filter.filter_type == "PRICE_FILTER") {
        price_filter = &filter;
      }
    }

    if (lot_filter) {
      related.min_trade_vol = parse_or_default(lot_filter->min_qty, 0.0);
      related.max_trade_vol = parse_or_default(lot_filter->max_qty, 0.0);
      related.min_qty_increment = parse_or_default(lot_filter->step_size, 0.0);
    }

    if (mlot_filter) {
      related.market_min_trade_vol =
          parse_or_default(mlot_filter->min_qty, related.min_trade_vol);
      related.market_max_trade_vol =
          parse_or_default(mlot_filter->max_qty, related.max_trade_vol);
      related.market_min_qty_increment =
          parse_or_default(mlot_filter->step_size, related.min_qty_increment);
    } else {
      related.market_min_trade_vol = related.min_trade_vol;
      related.market_max_trade_vol = related.max_trade_vol;
      related.market_min_qty_increment = related.min_qty_increment;
    }

    if (price_filter) {
      constexpr double kTickSize = 0.00001;
      related.min_price_increment =
          parse_or_default(price_filter->tick_size, kTickSize);
    }

    info.symbols.push_back(std::move(related));
  }
  return info;
}

MarketDataReject WsMdDomainMapper::to_reject(const WireMessage& msg) const {
  const auto* response = as_api_response(msg);
  MarketDataReject reject{};

  if (response->error.has_value()) {
    reject.error_code = response->error.value().code;
    reject.session_reject_reason = response->error.value().message;
    reject.error_message = response->error.value().message;
  }

  reject.rejected_message_type = 0;

  return reject;
}
MarketData* WsMdDomainMapper::make_entry(const std::string& symbol, Side side,
    double price, double qty, MarketUpdateType update_type) const {
  const auto update = qty <= 0.0 ? MarketUpdateType::kCancel : update_type;
  auto* entry = market_data_pool_->allocate(update,
      OrderId{0},
      TickerId{symbol},
      side,
      Price{price},
      Qty{qty});
  if (!entry) {
    logger_.error("Market data pool exhausted");
  }
  return entry;
}

MarketUpdateData WsMdDomainMapper::build_depth_update(const DepthResponse& msg,
    MarketDataType type) const {
  std::vector<MarketData*> entries;
  entries.reserve(msg.data.bids.size() + msg.data.asks.size());

  const auto& symbol = msg.data.symbol;
  for (const auto& bid : msg.data.bids) {
    entries.push_back(make_entry(symbol,
        common::Side::kBuy,
        bid[0],
        bid[1],
        common::MarketUpdateType::kAdd));
  }

  for (const auto& ask : msg.data.asks) {
    entries.push_back(make_entry(symbol,
        common::Side::kSell,
        ask[0],
        ask[1],
        common::MarketUpdateType::kAdd));
  }

  return MarketUpdateData(msg.data.start_update_id,
      msg.data.end_update_id,
      type,
      std::move(entries));
}

MarketUpdateData WsMdDomainMapper::build_depth_snapshot(
    const DepthSnapshot& msg, MarketDataType type) const {
  std::vector<MarketData*> entries;
  entries.reserve(msg.result.bids.size() + msg.result.asks.size() + 1);

  // Extract symbol from the id field (format: "snapshot_BTCUSDT")
  std::string symbol;
  const auto pos = msg.id.find('_');
  if (pos != std::string::npos && pos + 1 < msg.id.size()) {
    symbol = msg.id.substr(pos + 1);
  } else {
    symbol = INI_CONFIG.get("meta", "ticker");
  }

  //Clear data
  entries.push_back(market_data_pool_->allocate(MarketUpdateType::kClear,
      OrderId{},
      TickerId{symbol},
      Side::kInvalid,
      Price{},
      Qty{}));

  for (const auto& bid : msg.result.bids) {
    entries.push_back(make_entry(symbol,
        common::Side::kBuy,
        bid[0],
        bid[1],
        common::MarketUpdateType::kAdd));
  }

  for (const auto& ask : msg.result.asks) {
    entries.push_back(make_entry(symbol,
        common::Side::kSell,
        ask[0],
        ask[1],
        common::MarketUpdateType::kAdd));
  }

  return MarketUpdateData(msg.result.lastUpdateId,
      msg.result.lastUpdateId,
      type,
      std::move(entries));
}

MarketUpdateData WsMdDomainMapper::build_trade_update(
    const TradeEvent& msg) const {
  std::vector<MarketData*> entries;
  entries.reserve(1);

  const auto side = msg.data.is_buyer_market_maker ? Side::kSell : Side::kBuy;
  auto* entry = make_entry(msg.data.symbol,
      side,
      msg.data.price,
      msg.data.quantity,
      MarketUpdateType::kTrade);
  if (entry) {
    entries.push_back(entry);
  }

  return MarketUpdateData(-1, -1, MarketDataType::kTrade, std::move(entries));
}

const DepthResponse* WsMdDomainMapper::as_depth(const WireMessage& msg) const {
  return std::get_if<DepthResponse>(&msg);
}

const TradeEvent* WsMdDomainMapper::as_trade(const WireMessage& msg) const {
  return std::get_if<TradeEvent>(&msg);
}

const DepthSnapshot* WsMdDomainMapper::as_depth_snapshot(
    const WireMessage& msg) const {
  return std::get_if<DepthSnapshot>(&msg);
}
const ExchangeInfoResponse* WsMdDomainMapper::as_exchange_info(
    const WireMessage& msg) const {
  return std::get_if<ExchangeInfoResponse>(&msg);
}

const ApiResponse* WsMdDomainMapper::as_api_response(
    const WireMessage& msg) const {
  return std::get_if<ApiResponse>(&msg);
}
}  // namespace core