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

#include "ws_md_core.h"

#include "common/ini_config.hpp"
#include "schema/request/exchange_info.h"

using common::MarketUpdateType;
using common::OrderId;
using common::Price;
using common::Qty;
using common::Side;
using common::TickerId;
using schema::DepthResponse;
using schema::DepthSnapshot;
using schema::TradeEvent;

namespace core {
namespace {
constexpr std::string_view kTradeSuffix = "@trade";

const DepthResponse* as_depth(const WsMdCore::WireMessage& msg) {
  return std::get_if<DepthResponse>(&msg);
}

const TradeEvent* as_trade(const WsMdCore::WireMessage& msg) {
  return std::get_if<TradeEvent>(&msg);
}

const DepthSnapshot* as_depth_snapshot(const WsMdCore::WireMessage& msg) {
  return std::get_if<DepthSnapshot>(&msg);
}
const schema::ExchangeInfoResponse* as_exchange_info_response(
    const WsMdCore::WireMessage& msg) {
  return std::get_if<schema::ExchangeInfoResponse>(&msg);
}
}  // namespace

WsMdCore::WsMdCore(common::Logger* logger, common::MemoryPool<MarketData>* pool)
    : logger_(logger->make_producer()), market_data_pool_(pool) {}

std::string WsMdCore::create_market_data_subscription_message(
    const RequestId& /*request_id*/, const MarketDepthLevel& level,
    const SymbolId& symbol, bool /*subscribe*/) {
  if (symbol.empty()) {
    return {};
  }
  std::string upper_symbol = symbol;
  std::ranges::transform(upper_symbol,
      upper_symbol.begin(),
      [](unsigned char chr) { return static_cast<char>(std::toupper(chr)); });
  return std::format(
      R"({{"id":"snapshot_{}","method":"depth","params":{{"symbol":"{}","limit":{}}}}})",
      upper_symbol,
      upper_symbol,
      level);

  // if (symbol.empty()) {
  //   return {};
  // }
  //
  // std::string stream = symbol;
  // std::ranges::transform(stream, stream.begin(), [](unsigned char chr) {
  //   return static_cast<char>(std::tolower(chr));
  // });
  // stream.append(kDepthSuffix);
  // const std::string method =
  //     subscribe ? std::string(kSubscribe) : std::string(kUnsubscribe);
  //
  // return std::format(R"({{"method":"{}","params":["{}"],"id":{}}})",
  //     method,
  //     stream,
  //     request_sequence_++);
}

std::string WsMdCore::create_trade_data_subscription_message(
    const RequestId& /*request_id*/, const MarketDepthLevel& /*level*/,
    const SymbolId& symbol) const {
  if (symbol.empty()) {
    return {};
  }
  std::string stream = symbol;
  std::ranges::transform(stream, stream.begin(), [](const unsigned char chr) {
    return static_cast<char>(std::tolower(chr));
  });
  stream.append(kTradeSuffix);
  return std::format(R"({{"method":"SUBSCRIBE","params":["{}"],"id":{}}})",
      stream,
      request_sequence_++);
}

MarketUpdateData WsMdCore::build_depth_update(const DepthResponse& msg,
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

MarketUpdateData WsMdCore::build_depth_snapshot(const DepthSnapshot& msg,
    MarketDataType type) const {
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

MarketUpdateData WsMdCore::create_market_data_message(
    const WireMessage& msg) const {
  if (const auto* depth = as_depth(msg)) {
    return build_depth_update(*depth, MarketDataType::kMarket);
  }
  if (const auto* trade = as_trade(msg)) {
    return build_trade_update(*trade);
  }
  return MarketUpdateData{MarketDataType::kNone, {}};
}

MarketUpdateData WsMdCore::create_snapshot_data_message(
    const WireMessage& msg) const {
  if (const auto* snapshot = as_depth_snapshot(msg)) {
    return build_depth_snapshot(*snapshot, MarketDataType::kMarket);
  }
  logger_.error("Snapshot requested from non-depth wire message");
  return MarketUpdateData{MarketDataType::kNone, {}};
}

std::string WsMdCore::request_instrument_list_message(
    const std::string& symbol) const {
  schema::ExchangeInfoRequest request;
  request.id = "md_exchangeInfo";
  request.params.symbols = {symbol};
  return glz::write_json(request).value_or(std::string{});
}

InstrumentInfo WsMdCore::create_instrument_list_message(
    const WireMessage& msg) const {
  const auto* response = as_exchange_info_response(msg);

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

MarketDataReject WsMdCore::create_reject_message(
    const WireMessage& /*msg*/) const {
  MarketDataReject reject{};
  reject.session_reject_reason = "WebSocket";
  reject.error_code = 0;
  reject.rejected_message_type = 0;
  reject.error_message = "WebSocket feed rejected request";
  return reject;
}

WsMdCore::WireMessage WsMdCore::decode(std::string_view payload) const {
  if (payload.empty()) {
    return WireMessage{};
  }
  if (payload == "__CONNECTED__") {
    return WireMessage{};
  }

  if (payload.find("@depth") != std::string_view::npos) {
    return decode_or_log<DepthResponse>(payload, "[DepthStream]");
  }

  if (payload.find("@trade") != std::string_view::npos) {
    return decode_or_log<TradeEvent>(payload, "[TradeStream]");
  }

  if (payload.find("snapshot") != std::string_view::npos) {
    return decode_or_log<DepthSnapshot>(payload, "[DepthSnapshot]");
  }

  if (payload.find("exchangeInfo") != std::string_view::npos) {
    schema::ExchangeInfoResponse exchange;
    auto error_code =
        glz::read<glz::opts{.error_on_unknown_keys = 0}>(exchange, payload);

    if (error_code != glz::error_code::none) {
      const std::string_view view{payload.data(), payload.size()};
      auto msg = glz::format_error(error_code, view);
      logger_.error(std::format("Failed to [ExchangeInfo] payload:{}. msg:{}",
          payload,
          msg));
      return WireMessage{};
    }
    return WireMessage{std::in_place_type<schema::ExchangeInfoResponse>,
        exchange};
  }

  constexpr int kDefaultMinMessageLen = 100;
  logger_.warn(std::format("Unhandled websocket payload: {}",
      payload.substr(0,
          std::min<size_t>(payload.size(), kDefaultMinMessageLen))));
  return WireMessage{};
}

MarketData* WsMdCore::make_entry(const std::string& symbol, Side side,
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

MarketUpdateData WsMdCore::build_trade_update(const TradeEvent& msg) const {
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

std::string WsMdCore::extract_symbol(const WireMessage& msg) {
  if (const auto* depth = as_depth(msg)) {
    return depth->data.symbol;
  }
  if (const auto* trade = as_trade(msg)) {
    return trade->data.symbol;
  }
  if (const auto* snapshot = as_depth_snapshot(msg)) {
    // Extract from id field (format: "snapshot_BTCUSDT")
    const auto pos = snapshot->id.find('_');
    if (pos != std::string::npos && pos + 1 < snapshot->id.size()) {
      return snapshot->id.substr(pos + 1);
    }
  }
  return {};
}

template <class T>
WsMdCore::WireMessage WsMdCore::decode_or_log(std::string_view payload,
    std::string_view label) const {
  auto parsed = glz::read_json<T>(payload);
  if (UNLIKELY(!parsed)) {
    auto error_msg = glz::format_error(parsed.error(), payload);
    logger_.error(
        std::format("\x1b[31m Failed to decode {} response: "
                    "{}. payload:{} \x1b[0m",
            label,
            error_msg,
            payload));
    return WireMessage{};
  }
  return WireMessage{std::in_place_type<T>, std::move(*parsed)};
}

}  // namespace core
