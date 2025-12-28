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

#ifndef BINANCE_FUTURE_RING_BUFFER_CONVERTER_HPP
#define BINANCE_FUTURE_RING_BUFFER_CONVERTER_HPP

#include <common/logger.h>
#include <common/market_data_ring_buffer.hpp>

#include "market_data.h"
#include "schema/futures/response/book_ticker.h"
#include "schema/futures/response/depth_stream.h"
#include "schema/futures/response/exchange_info_response.h"
#include "schema/futures/response/snapshot.h"
#include "schema/futures/response/trade.h"
#include "types.h"

namespace ring_buffer {

/**
 * RingBuffer 기반 Market Data Converter
 *
 * 기존 BinanceFuturesMdMessageConverter와 달리:
 * - MemoryPool 할당 없음
 * - MarketDataRingBuffer에 직접 쓰기
 * - Zero-copy 방식
 */
class BinanceFuturesRingBufferConverter {
 public:
  explicit BinanceFuturesRingBufferConverter(
      const common::Logger::Producer& logger,
      common::MarketDataRingBuffer* buffer)
      : logger_(logger), buffer_(buffer) {
    symbol_ = INI_CONFIG.get("meta", "ticker");
  }

  // ============== Trade ==============

  bool write_trade(const schema::futures::TradeEvent& msg) {
    const auto side = msg.data.is_buyer_market_maker ? common::Side::kSell
                                                     : common::Side::kBuy;
    return buffer_->write_trade(side,
        common::Price{msg.data.price},
        common::Qty{msg.data.quantity});
  }

  // ============== BookTicker ==============

  bool write_book_ticker(const schema::futures::BookTickerEvent& msg) {
    return buffer_->write_book_ticker(common::Price{msg.data.best_bid_price},
        common::Qty{msg.data.best_bid_qty},
        common::Price{msg.data.best_ask_price},
        common::Qty{msg.data.best_ask_qty});
  }

  // ============== Depth ==============

  bool write_depth(const schema::futures::DepthResponse& msg) {
    const size_t bid_count = msg.data.bids.size();
    const size_t ask_count = msg.data.asks.size();
    const size_t total_count = bid_count + ask_count;

    if (total_count == 0) {
      return true;  // Nothing to write
    }

    // thread_local 임시 버퍼 사용 (stack에서 할당, 재사용)
    thread_local std::vector<common::MarketDataEntry> temp_entries;
    temp_entries.clear();
    temp_entries.reserve(total_count);

    // Bids
    for (const auto& bid : msg.data.bids) {
      const double qty = bid[1];
      const auto type = qty <= 0.0 ? common::MarketUpdateType::kCancel
                                   : common::MarketUpdateType::kAdd;
      temp_entries.push_back(
          {type, common::Side::kBuy, common::Price{bid[0]}, common::Qty{qty}});
    }

    // Asks
    for (const auto& ask : msg.data.asks) {
      const double qty = ask[1];
      const auto type = qty <= 0.0 ? common::MarketUpdateType::kCancel
                                   : common::MarketUpdateType::kAdd;
      temp_entries.push_back(
          {type, common::Side::kSell, common::Price{ask[0]}, common::Qty{qty}});
    }

    return buffer_->write_depth(msg.data.start_update_id,
        msg.data.end_update_id,
        msg.data.final_update_id_in_last_stream,
        temp_entries.data(),
        temp_entries.size());
  }

  // ============== Snapshot ==============

  bool write_snapshot(const schema::futures::DepthSnapshot& msg) {
    const size_t bid_count = msg.result.bids.size();
    const size_t ask_count = msg.result.asks.size();
    const size_t total_count = bid_count + ask_count;

    if (total_count == 0) {
      return true;
    }

    // thread_local 임시 버퍼 사용
    thread_local std::vector<common::MarketDataEntry> temp_entries;
    temp_entries.clear();
    temp_entries.reserve(total_count);

    // Bids
    for (const auto& [price, qty] : msg.result.bids) {
      temp_entries.push_back({common::MarketUpdateType::kAdd,
          common::Side::kBuy,
          common::Price{price},
          common::Qty{qty}});
    }

    // Asks
    for (const auto& [price, qty] : msg.result.asks) {
      temp_entries.push_back({common::MarketUpdateType::kAdd,
          common::Side::kSell,
          common::Price{price},
          common::Qty{qty}});
    }

    return buffer_->write_snapshot(msg.result.book_update_id,
        temp_entries.data(),
        temp_entries.size());
  }

  // ============== Visitor Pattern (기존 구조 호환) ==============

  struct MarketDataWriteVisitor {
    explicit MarketDataWriteVisitor(
        BinanceFuturesRingBufferConverter& converter)
        : converter_(converter) {}

    bool operator()(std::monostate) const {
      return true;  // No-op
    }

    bool operator()(const schema::futures::DepthResponse& msg) const {
      return converter_.write_depth(msg);
    }

    bool operator()(const schema::futures::TradeEvent& msg) const {
      return converter_.write_trade(msg);
    }

    bool operator()(const schema::futures::BookTickerEvent& msg) const {
      return converter_.write_book_ticker(msg);
    }

    bool operator()(const schema::futures::DepthSnapshot& msg) const {
      return converter_.write_snapshot(msg);
    }

    bool operator()(const schema::futures::ApiResponse& /*msg*/) const {
      return true;  // No-op for API responses
    }

    bool operator()(
        const schema::futures::ExchangeInfoHttpResponse& /*msg*/) const {
      return true;  // No-op for exchange info
    }

   private:
    BinanceFuturesRingBufferConverter& converter_;
  };

  [[nodiscard]] auto make_write_visitor() {
    return MarketDataWriteVisitor{*this};
  }

 private:
  [[maybe_unused]] const common::Logger::Producer& logger_;
  common::MarketDataRingBuffer* buffer_;
  std::string symbol_;
};

}  // namespace ring_buffer

#endif  // BINANCE_FUTURE_RING_BUFFER_CONVERTER_HPP
