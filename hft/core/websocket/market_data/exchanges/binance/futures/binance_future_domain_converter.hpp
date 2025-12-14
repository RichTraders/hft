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
#include <common/memory_pool.hpp>

#include "market_data.h"
#include "schema/futures/response/depth_stream.h"
#include "schema/futures/response/snapshot.h"
#include "schema/futures/response/trade.h"
#include "types.h"

inline MarketData* make_entry(common::MemoryPool<MarketData>* pool,
    const std::string& symbol, common::Side side, double price, double qty,
    common::MarketUpdateType update_type) {

  const auto update =
      qty <= 0.0 ? common::MarketUpdateType::kCancel : update_type;
  auto* entry = pool->allocate(update,
      common::OrderId{0},
      common::TickerId{symbol},
      side,
      common::Price{price},
      common::Qty{qty});
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
        : logger_(converter.logger_), pool_(converter.pool_) {}
    [[nodiscard]] MarketUpdateData operator()(std::monostate) const {
      logger_.debug("");
      return MarketUpdateData{};
    }
    [[nodiscard]] MarketUpdateData operator()(
        const schema::futures::DepthResponse& msg) const {
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
        const schema::futures::DepthSnapshot& msg) const {
      std::vector<MarketData*> entries;
      entries.reserve(msg.result.bids.size() + msg.result.asks.size() + 1);

      // TODO(JB): Remove Hardcoding
      const auto& symbol = "BTCUSDT";

      // Clear data
      entries.push_back(pool_->allocate(common::MarketUpdateType::kClear,
          common::OrderId{},
          common::TickerId{symbol},
          common::Side::kInvalid,
          common::Price{},
          common::Qty{}));

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

   private:
    const common::Logger::Producer& logger_;
    common::MemoryPool<MarketData>* pool_;
  };
  struct SnapshotVisitor {
    explicit SnapshotVisitor(const BinanceFuturesMdMessageConverter& converter)
        : logger_(converter.logger_), pool_(converter.pool_) {}

    [[nodiscard]] MarketData* make_entry(const std::string& symbol,
        common::Side side, double price, double qty,
        common::MarketUpdateType update_type) const {
      const auto update =
          qty <= 0.0 ? common::MarketUpdateType::kCancel : update_type;
      auto* entry = pool_->allocate(update,
          common::OrderId{0},
          common::TickerId{symbol},
          side,
          common::Price{price},
          common::Qty{qty});
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
          common::Price{},
          common::Qty{}));

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
