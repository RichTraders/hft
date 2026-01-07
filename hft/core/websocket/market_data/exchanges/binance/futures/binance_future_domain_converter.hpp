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

#ifndef BINANCE_FUTURE_MESSAGE_VISITOR_H
#define BINANCE_FUTURE_MESSAGE_VISITOR_H
#include <common/logger.h>
#include <common/performance.h>
#include <common/memory_pool.hpp>

#include "market_data.h"
#include "schema/futures/response/book_ticker.h"
#include "schema/futures/response/depth_stream.h"
#include "schema/futures/response/exchange_info_response.h"
#include "schema/futures/response/snapshot.h"
#include "schema/futures/response/trade.h"
#include "types.h"

inline MarketData* make_entry(common::MemoryPool<MarketData>* pool,
    const std::string& symbol, common::Side side, int64_t price, int64_t qty,
    common::MarketUpdateType update_type) {

  const auto update =
      qty <= 0 ? common::MarketUpdateType::kCancel : update_type;
  auto* entry = pool->allocate(update,
      common::OrderId{0},
      common::TickerId{symbol},
      side,
      common::PriceType::from_raw(price),
      common::QtyType::from_raw(qty));
  if (LIKELY(entry)) {
    return entry;
  }
  return nullptr;
}
struct BinanceFuturesMdMessageConverter {

  explicit BinanceFuturesMdMessageConverter(
      const common::Logger::Producer& logger,
      common::MemoryPool<MarketData>* pool)
      : logger_(logger), pool_(pool) {}
  [[nodiscard]] auto make_market_data_visitor() const {
    return MarketDataVisitor{*this};
  }
  [[nodiscard]] auto make_snapshot_visitor() const {
    return SnapshotVisitor{*this};
  }
  [[nodiscard]] auto make_instrument_visitor() const {
    return InstrumentInfoVisitor{*this};
  }
  [[nodiscard]] auto make_reject_visitor() const {
    return RejectVisitor{*this};
  }

 private:
  const common::Logger::Producer& logger_;
  common::MemoryPool<MarketData>* pool_;

 public:
  struct MarketDataVisitor {
    explicit MarketDataVisitor(
        const BinanceFuturesMdMessageConverter& converter)
        : logger_(converter.logger_), pool_(converter.pool_) {
      symbol_ = INI_CONFIG.get("meta", "ticker");
    }
    [[nodiscard]] MarketUpdateData operator()(std::monostate) const {
      logger_.debug("");
      return MarketUpdateData{};
    }
    [[nodiscard]] MarketUpdateData operator()(
        const schema::futures::DepthResponse& msg) const {
#ifdef ENABLE_CONVERTER_TIMING
      logger_.info("[CONVERTER_TIMING] DepthResponse E:{} T:{}",
          msg.data.timestamp,
          msg.data.transaction_time);
#endif
      std::vector<MarketData*> entries;
      entries.reserve(msg.data.bids.size() + msg.data.asks.size());

      const auto& symbol = msg.data.symbol;
      for (const auto& bid : msg.data.bids) {
        entries.push_back(make_entry(pool_,
            symbol,
            common::Side::kBuy,
            bid[0],
            bid[1],
            common::MarketUpdateType::kAdd));
      }

      for (const auto& ask : msg.data.asks) {
        entries.push_back(make_entry(pool_,
            symbol,
            common::Side::kSell,
            ask[0],
            ask[1],
            common::MarketUpdateType::kAdd));
      }

      MarketUpdateData result(msg.data.start_update_id,
          msg.data.end_update_id,
          MarketDataType::kMarket,
          std::move(entries));
      result.prev_end_idx = msg.data.final_update_id_in_last_stream;
      return result;
    }
    [[nodiscard]] MarketUpdateData operator()(
        const schema::futures::TradeEvent& msg) const {
#ifdef ENABLE_CONVERTER_TIMING
      logger_.info("[CONVERTER_TIMING] TradeEvent E:{} T:{} data:{}",
          msg.data.event_time,
          msg.data.trade_time,
          static_cast<int64_t>(msg.data.price));
#endif
      std::vector<MarketData*> entries;
      entries.reserve(1);

      const auto side = msg.data.is_buyer_market_maker ? common::Side::kSell
                                                       : common::Side::kBuy;
      auto* entry = make_entry(pool_,
          msg.data.symbol,
          side,
          msg.data.price,
          msg.data.quantity,
          common::MarketUpdateType::kTrade);
      if (entry) {
        entries.push_back(entry);
      }

      return MarketUpdateData(-1,
          -1,
          MarketDataType::kTrade,
          std::move(entries));
    }

    [[nodiscard]] MarketUpdateData operator()(
        const schema::futures::BookTickerEvent& msg) const {
#ifdef ENABLE_CONVERTER_TIMING
      logger_.info(
          "[CONVERTER_TIMING] BookTickerEvent E:{} T:{} bid:{}@{} ask:{}@{}",
          msg.data.event_time,
          msg.data.transaction_time,
          static_cast<int64_t>(msg.data.best_bid_price),
          static_cast<int64_t>(msg.data.best_bid_qty),
          static_cast<int64_t>(msg.data.best_ask_price),
          static_cast<int64_t>(msg.data.best_ask_qty));
#endif
      std::vector<MarketData*> entries;
      entries.reserve(2);

      const auto& symbol = msg.data.symbol;

      auto* bid_entry = make_entry(pool_,
          symbol,
          common::Side::kBuy,
          msg.data.best_bid_price,
          msg.data.best_bid_qty,
          common::MarketUpdateType::kBookTicker);
      if (bid_entry) {
        entries.push_back(bid_entry);
      }

      auto* ask_entry = make_entry(pool_,
          symbol,
          common::Side::kSell,
          msg.data.best_ask_price,
          msg.data.best_ask_qty,
          common::MarketUpdateType::kBookTicker);
      if (ask_entry) {
        entries.push_back(ask_entry);
      }

      return MarketUpdateData(static_cast<std::int64_t>(msg.data.update_id),
          static_cast<std::int64_t>(msg.data.update_id),
          MarketDataType::kBookTicker,
          std::move(entries));
    }

    [[nodiscard]] MarketUpdateData operator()(
        const schema::futures::DepthSnapshot& msg) const {
      std::vector<MarketData*> entries;
      entries.reserve(msg.result.bids.size() + msg.result.asks.size() + 1);

      // TODO(JB): Remove Hardcoding
      const auto& symbol = symbol_;

      // Clear data
      entries.push_back(pool_->allocate(common::MarketUpdateType::kClear,
          common::OrderId{},
          common::TickerId{symbol},
          common::Side::kInvalid,
          common::PriceType::from_raw(0),
          common::QtyType::from_raw(0)));

      for (const auto& [price, qty] : msg.result.bids) {
        entries.push_back(make_entry(pool_,
            symbol,
            common::Side::kBuy,
            price,
            qty,
            common::MarketUpdateType::kAdd));
      }

      for (const auto& [price, qty] : msg.result.asks) {
        entries.push_back(make_entry(pool_,
            symbol,
            common::Side::kSell,
            price,
            qty,
            common::MarketUpdateType::kAdd));
      }

      return MarketUpdateData(msg.result.book_update_id,
          msg.result.book_update_id,
          kMarket,
          std::move(entries));
    }

    [[nodiscard]] MarketUpdateData operator()(
        const schema::futures::ApiResponse& /*msg*/) const {
      logger_.debug("ApiResponse received in MarketDataVisitor");
      return MarketUpdateData{};
    }

    [[nodiscard]] MarketUpdateData operator()(
        const schema::futures::ExchangeInfoHttpResponse& /*msg*/) const {
      logger_.debug("ExchangeInfoHttpResponse received in MarketDataVisitor");
      return MarketUpdateData{};
    }

   private:
    const common::Logger::Producer& logger_;
    common::MemoryPool<MarketData>* pool_;
    std::string symbol_;
  };
  struct SnapshotVisitor {
    explicit SnapshotVisitor(const BinanceFuturesMdMessageConverter& converter)
        : logger_(converter.logger_), pool_(converter.pool_) {}

    [[nodiscard]] MarketData* make_entry(const std::string& symbol,
        common::Side side, int64_t price, int64_t qty,
        common::MarketUpdateType update_type) const {
      const auto update =
          qty <= 0 ? common::MarketUpdateType::kCancel : update_type;
      auto* entry = pool_->allocate(update,
          common::OrderId{0},
          common::TickerId{symbol},
          side,
          common::PriceType::from_raw(price),
          common::QtyType::from_raw(qty));
      if (!entry) {
        logger_.error("Market data pool exhausted");
      }
      return entry;
    }

    [[nodiscard]] MarketUpdateData operator()(std::monostate) const {
      return MarketUpdateData{};
    }

    [[nodiscard]] MarketUpdateData operator()(
        const schema::futures::DepthSnapshot& msg) const {
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

      entries.push_back(pool_->allocate(common::MarketUpdateType::kClear,
          common::OrderId{},
          common::TickerId{symbol},
          common::Side::kInvalid,
          common::PriceType::from_raw(0),
          common::QtyType::from_raw(0)));

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

      return MarketUpdateData(msg.result.book_update_id,
          msg.result.book_update_id,
          MarketDataType::kMarket,
          std::move(entries));
    }

    template <typename T>
    [[nodiscard]] MarketUpdateData operator()(const T&) const {
      logger_.error("Snapshot requested from non-depth wire message");
      return MarketUpdateData{};
    }

   private:
    const common::Logger::Producer& logger_;
    common::MemoryPool<MarketData>* pool_;
  };
  struct InstrumentInfoVisitor {
    explicit InstrumentInfoVisitor(
        const BinanceFuturesMdMessageConverter& converter)
        : logger_(converter.logger_) {}

    [[nodiscard]] InstrumentInfo operator()(std::monostate) const { return {}; }

    [[nodiscard]] InstrumentInfo operator()(
        const schema::futures::ExchangeInfoHttpResponse& payload) const {
      InstrumentInfo info;
      info.instrument_req_id = "futures_http";
      const auto& symbols = payload.symbols;
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

        const schema::futures::SymbolFilter* lot_filter = nullptr;
        const schema::futures::SymbolFilter* mlot_filter = nullptr;
        const schema::futures::SymbolFilter* price_filter = nullptr;

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
          related.min_qty_increment =
              parse_or_default(lot_filter->step_size, 0.0);
        }

        if (mlot_filter) {
          related.market_min_trade_vol =
              parse_or_default(mlot_filter->min_qty, related.min_trade_vol);
          related.market_max_trade_vol =
              parse_or_default(mlot_filter->max_qty, related.max_trade_vol);
          related.market_min_qty_increment =
              parse_or_default(mlot_filter->step_size,
                  related.min_qty_increment);
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

    template <typename T>
    [[nodiscard]] InstrumentInfo operator()(const T&) const {
      logger_.info("");
      return {};
    }

   private:
    const common::Logger::Producer& logger_;
  };
  struct RejectVisitor {
    explicit RejectVisitor(const BinanceFuturesMdMessageConverter& converter)
        : logger_(converter.logger_) {}
    [[nodiscard]] MarketDataReject operator()(
        const schema::futures::ApiResponse& msg) const {
      logger_.info("");
      MarketDataReject reject{};
      if (msg.error.has_value()) {
        reject.error_code = msg.error.value().code;
        reject.session_reject_reason = msg.error.value().message;
        reject.error_message = msg.error.value().message;
      }
      reject.rejected_message_type = 0;
      return reject;
    }
    template <typename T>
    [[nodiscard]] MarketDataReject operator()(const T& /*msg*/) {
      return {};
    }

   private:
    const common::Logger::Producer& logger_;
  };
};
#endif  //BINANCE_FUTURE_MESSAGE_VISITOR_H
