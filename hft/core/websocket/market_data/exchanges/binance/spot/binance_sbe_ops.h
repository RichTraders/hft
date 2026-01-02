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

#ifndef BINANCE_SBE_OPS_H
#define BINANCE_SBE_OPS_H

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>
#include <vector>

#include <common/logger.h>
#include "schema/spot/sbe/best_bid_ask_sbe.h"
#include "schema/spot/sbe/depth_stream_sbe.h"
#include "schema/spot/sbe/snapshot_sbe.h"
#include "schema/spot/sbe/trade_sbe.h"

namespace schema::sbe {
struct SBEMessageHeader {
  uint16_t block_length;
  uint16_t template_id;
  uint16_t schema_id;
  uint16_t version;
};

struct GroupSize16 {
  uint16_t block_length;
  uint16_t num_in_group;
};

struct GroupSize32 {
  uint16_t block_length;
  uint32_t num_in_group;
};

constexpr size_t kHeaderSize = sizeof(SBEMessageHeader);

inline double decode_mantissa(int64_t mantissa, int8_t exponent) {
  constexpr auto kBase = 10.0;
  return static_cast<double>(mantissa) * std::pow(kBase, exponent);
}

inline const char* parse_group_header(const char* pos,
    schema::sbe::GroupSize16& size) {
  std::memcpy(&size.block_length, pos, sizeof(uint16_t));
  pos += sizeof(uint16_t);
  std::memcpy(&size.num_in_group, pos, sizeof(uint16_t));
  pos += sizeof(uint16_t);
  return pos;
}

inline const char* parse_group_header(const char* pos, GroupSize32& size) {
  std::memcpy(&size.block_length, pos, sizeof(uint16_t));
  pos += sizeof(uint16_t);
  std::memcpy(&size.num_in_group, pos, sizeof(uint32_t));
  pos += sizeof(uint32_t);
  return pos;
}

inline const char* parse_varString8(const char* pos, std::string& str) {
  uint8_t length;
  std::memcpy(&length, pos, sizeof(uint8_t));
  pos += sizeof(uint8_t);

  if (length > 0) {
    str.assign(pos, length);
    pos += length;
  } else {
    str.clear();
  }

  return pos;
}

inline std::array<double, 2> decode_price_level(const char*& pos,
    int8_t price_exponent, int8_t qty_exponent) {

  int64_t price_mantissa;
  int64_t qty_mantissa;

  std::memcpy(&price_mantissa, pos, sizeof(int64_t));
  pos += sizeof(int64_t);
  std::memcpy(&qty_mantissa, pos, sizeof(int64_t));
  pos += sizeof(int64_t);

  const double price =
      schema::sbe::decode_mantissa(price_mantissa, price_exponent);
  const double qty = schema::sbe::decode_mantissa(qty_mantissa, qty_exponent);

  return {price, qty};
}
}  // namespace schema::sbe

struct BinanceSbeOps {
  using MessageHeader = schema::sbe::SBEMessageHeader;
  static constexpr size_t kHeaderSize = schema::sbe::kHeaderSize;
  static constexpr int kTradesStreamEventId = 10000;
  static constexpr int kBestBidAskStreamEventId = 10001;
  static constexpr int kDepthSnapshotStreamEventId = 10002;
  static constexpr int kDepthDiffStreamEventId = 10003;
  // Decode TradesStreamEvent (template ID 10000)
  // Format: eventTime(8) + transactTime(8) + priceExponent(1) + qtyExponent(1)
  //         + trades_group + symbol_varString8
  schema::sbe::SbeTradeEvent decode_trade_event(const char* pos,
      size_t remaining, const common::Logger::Producer& logger) const {
    constexpr size_t kMinSize = sizeof(int64_t) * 2 + sizeof(int8_t) * 2 +
                                sizeof(schema::sbe::GroupSize32);
    if (UNLIKELY(remaining < kMinSize)) {
      logger.error("TradeEvent: insufficient buffer (need {}, have {})",
          kMinSize,
          remaining);
      return schema::sbe::SbeTradeEvent{};
    }

    const char* const start = pos;

    schema::sbe::SbeTradeEvent event;

    std::memcpy(&event.event_time, pos, sizeof(int64_t));
    pos += sizeof(int64_t);
    std::memcpy(&event.transact_time, pos, sizeof(int64_t));
    pos += sizeof(int64_t);

    int8_t price_exponent;
    int8_t qty_exponent;
    std::memcpy(&price_exponent, pos, sizeof(int8_t));
    pos += sizeof(int8_t);
    std::memcpy(&qty_exponent, pos, sizeof(int8_t));
    pos += sizeof(int8_t);

    schema::sbe::GroupSize32 group_size;
    pos = parse_group_header(pos, group_size);

    constexpr size_t kTradeEntrySize =
        sizeof(int64_t) * 3 + sizeof(uint8_t);  // isBestMatch is constant
    const size_t trades_total_size =
        static_cast<size_t>(group_size.block_length) * group_size.num_in_group;
    if (UNLIKELY(
            group_size.block_length < kTradeEntrySize ||
            remaining < static_cast<size_t>(pos - start) + trades_total_size)) {
      logger.error("TradeEvent: trades group size exceeds buffer");
      return schema::sbe::SbeTradeEvent{};
    }

    event.trades.reserve(group_size.num_in_group);
    for (uint32_t idx = 0; idx < group_size.num_in_group; ++idx) {
      const char* const entry_start = pos;
      schema::sbe::SbeTrade trade;

      size_t offset = 0;
      std::memcpy(&trade.id, entry_start + offset, sizeof(int64_t));
      offset += sizeof(int64_t);

      int64_t price_mantissa;
      int64_t qty_mantissa;
      std::memcpy(&price_mantissa, entry_start + offset, sizeof(int64_t));
      offset += sizeof(int64_t);
      std::memcpy(&qty_mantissa, entry_start + offset, sizeof(int64_t));
      offset += sizeof(int64_t);

      trade.price =
          schema::sbe::decode_mantissa(price_mantissa, price_exponent);
      trade.qty = schema::sbe::decode_mantissa(qty_mantissa, qty_exponent);

      uint8_t is_buyer_maker;
      std::memcpy(&is_buyer_maker, entry_start + offset, sizeof(uint8_t));

      trade.is_buyer_maker = (is_buyer_maker != 0);
      trade.is_best_match = true;  // Constant value in schema

      pos = entry_start + group_size.block_length;
      event.trades.push_back(std::move(trade));
    }

    pos = schema::sbe::parse_varString8(pos, event.symbol);

    return schema::sbe::SbeTradeEvent{std::move(event)};
  }

  // Decode BestBidAskStreamEvent (template ID 10001)
  // Format: eventTime(8) + bookUpdateId(8) + priceExponent(1) + qtyExponent(1)
  //         + bidPrice(8) + bidQty(8) + askPrice(8) + askQty(8) + symbol_varString8
  schema::sbe::SbeBestBidAsk decode_best_bid_ask(const char* pos,
      size_t remaining, const common::Logger::Producer& logger) const {
    constexpr size_t kMinSize = sizeof(int64_t) * 6 + sizeof(int8_t) * 2;
    if (UNLIKELY(remaining < kMinSize)) {
      logger.error(
          std::format("BestBidAsk: insufficient buffer (need {}, have {})",
              kMinSize,
              remaining));
      return schema::sbe::SbeBestBidAsk{};
    }

    schema::sbe::SbeBestBidAsk event;

    std::memcpy(&event.event_time, pos, sizeof(int64_t));
    pos += sizeof(int64_t);
    std::memcpy(&event.book_update_id, pos, sizeof(int64_t));
    pos += sizeof(int64_t);

    int8_t price_exponent;
    int8_t qty_exponent;
    std::memcpy(&price_exponent, pos, sizeof(int8_t));
    pos += sizeof(int8_t);
    std::memcpy(&qty_exponent, pos, sizeof(int8_t));
    pos += sizeof(int8_t);

    int64_t bid_price_mantissa;
    int64_t bid_qty_mantissa;
    int64_t ask_price_mantissa;
    int64_t ask_qty_mantissa;

    std::memcpy(&bid_price_mantissa, pos, sizeof(int64_t));
    pos += sizeof(int64_t);
    std::memcpy(&bid_qty_mantissa, pos, sizeof(int64_t));
    pos += sizeof(int64_t);
    std::memcpy(&ask_price_mantissa, pos, sizeof(int64_t));
    pos += sizeof(int64_t);
    std::memcpy(&ask_qty_mantissa, pos, sizeof(int64_t));
    pos += sizeof(int64_t);

    event.bid_price =
        schema::sbe::decode_mantissa(bid_price_mantissa, price_exponent);
    event.bid_qty =
        schema::sbe::decode_mantissa(bid_qty_mantissa, qty_exponent);
    event.ask_price =
        schema::sbe::decode_mantissa(ask_price_mantissa, price_exponent);
    event.ask_qty =
        schema::sbe::decode_mantissa(ask_qty_mantissa, qty_exponent);

    pos = schema::sbe::parse_varString8(pos, event.symbol);

    return schema::sbe::SbeBestBidAsk{std::move(event)};
  }

  // Decode DepthSnapshotStreamEvent (template ID 10002)
  // Format: eventTime(8) + bookUpdateId(8) + priceExponent(1) + qtyExponent(1)
  //         + bids_group + asks_group + symbol_varString8
  schema::sbe::SbeDepthSnapshot decode_depth_snapshot(const char* pos,
      size_t remaining, const common::Logger::Producer& logger) const {

    constexpr size_t kMinSize = sizeof(int64_t) * 2 + sizeof(int8_t) * 2 +
                                sizeof(schema::sbe::GroupSize16) * 2;
    if (UNLIKELY(remaining < kMinSize)) {
      logger.error(
          std::format("DepthSnapshot: insufficient buffer (need {}, have {})",
              kMinSize,
              remaining));
      return schema::sbe::SbeDepthSnapshot{};
    }

    const char* const start = pos;
    constexpr size_t kLevelSize = sizeof(int64_t) * 2;

    schema::sbe::SbeDepthSnapshot event;

    std::memcpy(&event.event_time, pos, sizeof(int64_t));
    pos += sizeof(int64_t);
    std::memcpy(&event.book_update_id, pos, sizeof(int64_t));
    pos += sizeof(int64_t);

    int8_t price_exponent;
    int8_t qty_exponent;
    std::memcpy(&price_exponent, pos, sizeof(int8_t));
    pos += sizeof(int8_t);
    std::memcpy(&qty_exponent, pos, sizeof(int8_t));
    pos += sizeof(int8_t);

    schema::sbe::GroupSize16 bids_group;
    pos = parse_group_header(pos, bids_group);
    const size_t bids_total_size =
        static_cast<size_t>(bids_group.block_length) * bids_group.num_in_group;
    if (UNLIKELY(bids_group.block_length < kLevelSize ||
                 remaining < static_cast<size_t>(pos - start) +
                                 bids_total_size +
                                 sizeof(schema::sbe::GroupSize16))) {
      logger.error("DepthSnapshot: bids group size exceeds buffer");
      return schema::sbe::SbeDepthSnapshot{};
    }
    event.bids.reserve(bids_group.num_in_group);

    for (uint16_t idx = 0; idx < bids_group.num_in_group; ++idx) {
      const char* const entry_start = pos;
      event.bids.push_back(
          schema::sbe::decode_price_level(pos, price_exponent, qty_exponent));
      pos = entry_start + bids_group.block_length;
    }

    schema::sbe::GroupSize16 asks_group;
    pos = parse_group_header(pos, asks_group);
    const size_t asks_total_size =
        static_cast<size_t>(asks_group.block_length) * asks_group.num_in_group;
    if (UNLIKELY(
            asks_group.block_length < kLevelSize ||
            remaining < static_cast<size_t>(pos - start) + asks_total_size)) {
      logger.error("DepthSnapshot: asks group size exceeds buffer");
      return schema::sbe::SbeDepthSnapshot{};
    }
    event.asks.reserve(asks_group.num_in_group);

    for (uint16_t i = 0; i < asks_group.num_in_group; ++i) {
      const char* const entry_start = pos;
      event.asks.push_back(
          schema::sbe::decode_price_level(pos, price_exponent, qty_exponent));
      pos = entry_start + asks_group.block_length;
    }

    const auto consumed = static_cast<size_t>(pos - start);
    if (UNLIKELY(remaining < consumed + sizeof(uint8_t))) {
      logger.error("DepthSnapshot: missing symbol length");
      return schema::sbe::SbeDepthSnapshot{};
    }
    uint8_t symbol_length;
    std::memcpy(&symbol_length, pos, sizeof(uint8_t));
    if (UNLIKELY(remaining < consumed + sizeof(uint8_t) + symbol_length)) {
      logger.error("DepthSnapshot: symbol exceeds buffer");
      return schema::sbe::SbeDepthSnapshot{};
    }

    pos = schema::sbe::parse_varString8(pos, event.symbol);

    return schema::sbe::SbeDepthSnapshot{std::move(event)};
  }

  // Decode DepthDiffStreamEvent (template ID 10003)
  // Format: eventTime(8) + firstBookUpdateId(8) + lastBookUpdateId(8)
  //         + priceExponent(1) + qtyExponent(1) + bids_group + asks_group + symbol_varString8
  schema::sbe::SbeDepthResponse decode_depth_diff(const char* pos,
      size_t remaining, const common::Logger::Producer& logger) const {

    constexpr size_t kMinSize = sizeof(int64_t) * 3 + sizeof(int8_t) * 2 +
                                sizeof(schema::sbe::GroupSize16) * 2;
    if (UNLIKELY(remaining < kMinSize)) {
      logger.error(
          std::format("DepthDiff: insufficient buffer (need {}, have {})",
              kMinSize,
              remaining));
      return schema::sbe::SbeDepthResponse{};
    }

    const char* const start = pos;
    constexpr size_t kLevelSize = sizeof(int64_t) * 2;

    schema::sbe::SbeDepthResponse event;

    std::memcpy(&event.event_time, pos, sizeof(int64_t));
    pos += sizeof(int64_t);
    std::memcpy(&event.first_book_update_id, pos, sizeof(int64_t));
    pos += sizeof(int64_t);
    std::memcpy(&event.last_book_update_id, pos, sizeof(int64_t));
    pos += sizeof(int64_t);

    int8_t price_exponent;
    int8_t qty_exponent;
    std::memcpy(&price_exponent, pos, sizeof(int8_t));
    pos += sizeof(int8_t);
    std::memcpy(&qty_exponent, pos, sizeof(int8_t));
    pos += sizeof(int8_t);

    schema::sbe::GroupSize16 bids_group;
    pos = parse_group_header(pos, bids_group);
    const size_t bids_total_size =
        static_cast<size_t>(bids_group.block_length) * bids_group.num_in_group;
    if (UNLIKELY(bids_group.block_length < kLevelSize ||
                 remaining < static_cast<size_t>(pos - start) +
                                 bids_total_size +
                                 sizeof(schema::sbe::GroupSize16))) {
      logger.error("DepthDiff: bids group size exceeds buffer");
      return schema::sbe::SbeDepthResponse{};
    }
    event.bids.reserve(bids_group.num_in_group);

    for (uint16_t idx = 0; idx < bids_group.num_in_group; ++idx) {
      const char* const entry_start = pos;
      event.bids.push_back(
          schema::sbe::decode_price_level(pos, price_exponent, qty_exponent));
      pos = entry_start + bids_group.block_length;
    }

    schema::sbe::GroupSize16 asks_group;
    pos = parse_group_header(pos, asks_group);
    const size_t asks_total_size =
        static_cast<size_t>(asks_group.block_length) * asks_group.num_in_group;
    if (UNLIKELY(
            asks_group.block_length < kLevelSize ||
            remaining < static_cast<size_t>(pos - start) + asks_total_size)) {
      logger.error("DepthDiff: asks group size exceeds buffer");
      return schema::sbe::SbeDepthResponse{};
    }
    event.asks.reserve(asks_group.num_in_group);

    for (uint16_t i = 0; i < asks_group.num_in_group; ++i) {
      const char* const entry_start = pos;
      event.asks.push_back(
          schema::sbe::decode_price_level(pos, price_exponent, qty_exponent));
      pos = entry_start + asks_group.block_length;
    }

    const auto consumed = static_cast<size_t>(pos - start);
    if (UNLIKELY(remaining < consumed + sizeof(uint8_t))) {
      logger.error("DepthDiff: missing symbol length");
      return schema::sbe::SbeDepthResponse{};
    }
    uint8_t symbol_length;
    std::memcpy(&symbol_length, pos, sizeof(uint8_t));
    if (UNLIKELY(remaining < consumed + sizeof(uint8_t) + symbol_length)) {
      logger.error("DepthDiff: symbol exceeds buffer");
      return schema::sbe::SbeDepthResponse{};
    }

    pos = schema::sbe::parse_varString8(pos, event.symbol);

    return schema::sbe::SbeDepthResponse{std::move(event)};
  }
};
#endif  //BINANCE_SBE_OPS_H
