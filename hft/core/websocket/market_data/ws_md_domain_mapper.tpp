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

#include "common/ini_config.hpp"
#include "common/types.h"
#include "schema/response/api_response.h"
#include "schema/response/depth_stream.h"
#include "schema/response/exchange_info_response.h"
#include "schema/response/snapshot.h"
#include "schema/response/trade.h"
#include "schema/sbe/best_bid_ask_sbe.h"
#include "schema/sbe/depth_stream_sbe.h"
#include "schema/sbe/snapshot_sbe.h"
#include "schema/sbe/trade_sbe.h"

namespace core {

using common::MarketUpdateType;
using common::OrderId;
using common::Price;
using common::Qty;
using common::TickerId;

// ============================================================================
// Public API - std::visit with if constexpr for type dispatch
// ============================================================================

template <DecoderPolicy Policy>
MarketUpdateData WsMdDomainMapper<Policy>::to_market_data(
    const WireMessage& msg) const {

    return std::visit([this](const auto& payload) -> MarketUpdateData {
        using T = std::decay_t<decltype(payload)>;

        if constexpr (std::is_same_v<T, std::monostate>) {
            return MarketUpdateData{MarketDataType::kNone, {}};
        }
        // JSON types
        else if constexpr (std::is_same_v<T, schema::DepthResponse>) {
            return build_json_depth_update(payload, MarketDataType::kMarket);
        }
        else if constexpr (std::is_same_v<T, schema::TradeEvent>) {
            return build_json_trade_update(payload);
        }
        // SBE types
        else if constexpr (std::is_same_v<T, schema::sbe::SbeDepthResponse>) {
            return build_sbe_depth_update(payload, MarketDataType::kMarket);
        }
        else if constexpr (std::is_same_v<T, schema::sbe::SbeTradeEvent>) {
            return build_sbe_trade_update(payload);
        }
        else if constexpr (std::is_same_v<T, schema::sbe::SbeBestBidAsk>) {
            return build_sbe_best_bid_ask(payload);
        }
        else {
            // Unhandled type (e.g., ExchangeInfoResponse, ApiResponse)
            return MarketUpdateData{MarketDataType::kNone, {}};
        }
    }, msg);
}

template <DecoderPolicy Policy>
MarketUpdateData WsMdDomainMapper<Policy>::to_snapshot_data(
    const WireMessage& msg) const {

    return std::visit([this](const auto& payload) -> MarketUpdateData {
        using T = std::decay_t<decltype(payload)>;

        if constexpr (std::is_same_v<T, std::monostate>) {
            return MarketUpdateData{MarketDataType::kNone, {}};
        }
        // JSON snapshot
        else if constexpr (std::is_same_v<T, schema::DepthSnapshot>) {
            return build_json_depth_snapshot(payload, MarketDataType::kMarket);
        }
        // SBE snapshot
        else if constexpr (std::is_same_v<T, schema::sbe::SbeDepthSnapshot>) {
            return build_sbe_depth_snapshot(payload, MarketDataType::kMarket);
        }
        else {
            logger_.error("Snapshot requested from non-depth wire message");
            return MarketUpdateData{MarketDataType::kNone, {}};
        }
    }, msg);
}

template <DecoderPolicy Policy>
InstrumentInfo WsMdDomainMapper<Policy>::to_instrument_info(
    const WireMessage& msg) const {

    return std::visit([](const auto& payload) -> InstrumentInfo {
        using T = std::decay_t<decltype(payload)>;

        // ExchangeInfoResponse is common to both JSON and SBE policies
        if constexpr (std::is_same_v<T, schema::ExchangeInfoResponse>) {
            InstrumentInfo info;
            info.instrument_req_id = payload.id;
            const auto& symbols = payload.result.symbols;
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
        else {
            return InstrumentInfo{};
        }
    }, msg);
}

template <DecoderPolicy Policy>
MarketDataReject WsMdDomainMapper<Policy>::to_reject(
    const WireMessage& msg) const {

    return std::visit([](const auto& payload) -> MarketDataReject {
        using T = std::decay_t<decltype(payload)>;

        // ApiResponse is common to both JSON and SBE policies
        if constexpr (std::is_same_v<T, schema::ApiResponse>) {
            MarketDataReject reject{};
            if (payload.error.has_value()) {
                reject.error_code = payload.error.value().code;
                reject.session_reject_reason = payload.error.value().message;
                reject.error_message = payload.error.value().message;
            }
            reject.rejected_message_type = 0;
            return reject;
        }
        else {
            return MarketDataReject{};
        }
    }, msg);
}

// ============================================================================
// Common helper - Policy-independent
// ============================================================================

template <DecoderPolicy Policy>
MarketData* WsMdDomainMapper<Policy>::make_entry(
    const std::string& symbol, Side side,
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

// ============================================================================
// JSON-specific builders
// ============================================================================

template <DecoderPolicy Policy>
MarketUpdateData WsMdDomainMapper<Policy>::build_json_depth_update(
    const schema::DepthResponse& msg, MarketDataType type) const {

    std::vector<MarketData*> entries;
    entries.reserve(msg.data.bids.size() + msg.data.asks.size());

    const auto& symbol = msg.data.symbol;
    for (const auto& bid : msg.data.bids) {
        entries.push_back(make_entry(symbol,
            Side::kBuy,
            bid[0],
            bid[1],
            MarketUpdateType::kAdd));
    }

    for (const auto& ask : msg.data.asks) {
        entries.push_back(make_entry(symbol,
            Side::kSell,
            ask[0],
            ask[1],
            MarketUpdateType::kAdd));
    }

    return MarketUpdateData(msg.data.start_update_id,
        msg.data.end_update_id,
        type,
        std::move(entries));
}

template <DecoderPolicy Policy>
MarketUpdateData WsMdDomainMapper<Policy>::build_json_depth_snapshot(
    const schema::DepthSnapshot& msg, MarketDataType type) const {

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

    entries.push_back(market_data_pool_->allocate(MarketUpdateType::kClear,
        OrderId{},
        TickerId{symbol},
        Side::kInvalid,
        Price{},
        Qty{}));

    for (const auto& bid : msg.result.bids) {
        entries.push_back(make_entry(symbol,
            Side::kBuy,
            bid[0],
            bid[1],
            MarketUpdateType::kAdd));
    }

    for (const auto& ask : msg.result.asks) {
        entries.push_back(make_entry(symbol,
            Side::kSell,
            ask[0],
            ask[1],
            MarketUpdateType::kAdd));
    }

    return MarketUpdateData(msg.result.lastUpdateId,
        msg.result.lastUpdateId,
        type,
        std::move(entries));
}

template <DecoderPolicy Policy>
MarketUpdateData WsMdDomainMapper<Policy>::build_json_trade_update(
    const schema::TradeEvent& msg) const {

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

// ============================================================================
// SBE-specific builders
// ============================================================================

template <DecoderPolicy Policy>
MarketUpdateData WsMdDomainMapper<Policy>::build_sbe_depth_update(
    const schema::sbe::SbeDepthResponse& msg, MarketDataType type) const {

    std::vector<MarketData*> entries;
    entries.reserve(msg.bids.size() + msg.asks.size());

    const auto& symbol = msg.symbol;
    for (const auto& [price, qty] : msg.bids) {
        entries.push_back(make_entry(symbol, Side::kBuy, price, qty,
                                     MarketUpdateType::kAdd));
    }

    for (const auto& [price, qty] : msg.asks) {
        entries.push_back(make_entry(symbol, Side::kSell, price, qty,
                                     MarketUpdateType::kAdd));
    }

    return MarketUpdateData(msg.first_book_update_id, msg.last_book_update_id,
                            type, std::move(entries));
}

template <DecoderPolicy Policy>
MarketUpdateData WsMdDomainMapper<Policy>::build_sbe_depth_snapshot(
    const schema::sbe::SbeDepthSnapshot& msg, MarketDataType type) const {

    std::vector<MarketData*> entries;
    entries.reserve(msg.bids.size() + msg.asks.size() + 1);

    const auto& symbol = msg.symbol;

    // Clear data
    entries.push_back(market_data_pool_->allocate(MarketUpdateType::kClear,
        OrderId{},
        TickerId{symbol},
        Side::kInvalid,
        Price{},
        Qty{}));

    for (const auto& [price, qty] : msg.bids) {
        entries.push_back(make_entry(symbol, Side::kBuy, price, qty,
                                     MarketUpdateType::kAdd));
    }

    for (const auto& [price, qty] : msg.asks) {
        entries.push_back(make_entry(symbol, Side::kSell, price, qty,
                                     MarketUpdateType::kAdd));
    }

    return MarketUpdateData(msg.book_update_id, msg.book_update_id,
                            type, std::move(entries));
}

template <DecoderPolicy Policy>
MarketUpdateData WsMdDomainMapper<Policy>::build_sbe_trade_update(
    const schema::sbe::SbeTradeEvent& msg) const {

    std::vector<MarketData*> entries;
    entries.reserve(msg.trades.size());

    const auto& symbol = msg.symbol;
    for (const auto& trade : msg.trades) {
        const auto side = trade.is_buyer_maker ? Side::kSell : Side::kBuy;
        auto* entry = make_entry(symbol, side, trade.price, trade.qty,
                                 MarketUpdateType::kTrade);
        if (entry) {
            entries.push_back(entry);
        }
    }

    return MarketUpdateData(-1, -1, MarketDataType::kTrade, std::move(entries));
}

template <DecoderPolicy Policy>
MarketUpdateData WsMdDomainMapper<Policy>::build_sbe_best_bid_ask(
    const schema::sbe::SbeBestBidAsk& msg) const {

    std::vector<MarketData*> entries;
    entries.reserve(2);

    const auto& symbol = msg.symbol;

    entries.push_back(make_entry(symbol, Side::kBuy,
                                 msg.bid_price, msg.bid_qty,
                                 MarketUpdateType::kAdd));

    entries.push_back(make_entry(symbol, Side::kSell,
                                 msg.ask_price, msg.ask_qty,
                                 MarketUpdateType::kAdd));

    return MarketUpdateData(msg.book_update_id, msg.book_update_id,
                            MarketDataType::kMarket, std::move(entries));
}

}  // namespace core
