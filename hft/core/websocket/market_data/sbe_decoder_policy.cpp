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

#include <glaze/glaze.hpp>
#include "decoder_policy.h"
#include "schema/sbe/ws_md_sbe_decoder_impl.h"

namespace core {

namespace {

using schema::sbe::decode_mantissa;
using schema::sbe::decode_price_level;
using schema::sbe::GroupSize16;
using schema::sbe::GroupSize32;
using schema::sbe::kHeaderSize;
using schema::sbe::parse_group_header;
using schema::sbe::parse_varString8;
using schema::sbe::SBEMessageHeader;

constexpr int kTradesStreamEventId = 10000;
constexpr int kBestBidAskStreamEventId = 10001;
constexpr int kDepthSnapshotStreamEventId = 10002;
constexpr int kDepthDiffStreamEventId = 10003;

// Decode TradesStreamEvent (template ID 10000)
// Format: eventTime(8) + transactTime(8) + priceExponent(1) + qtyExponent(1)
//         + trades_group + symbol_varString8
SbeDecoderPolicy::WireMessage decode_trade_event(const char* pos,
    size_t remaining, const common::Logger::Producer& logger) {

  constexpr size_t kMinSize =
      sizeof(int64_t) * 2 + sizeof(int8_t) * 2 + sizeof(GroupSize32);
  if (UNLIKELY(remaining < kMinSize)) {
    logger.error(
        std::format("TradeEvent: insufficient buffer (need {}, have {})",
            kMinSize,
            remaining));
    return SbeDecoderPolicy::WireMessage{};
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

  GroupSize32 group_size;
  pos = parse_group_header(pos, group_size);

  constexpr size_t kTradeEntrySize =
      sizeof(int64_t) * 3 + sizeof(uint8_t);  // isBestMatch is constant
  const size_t trades_total_size =
      static_cast<size_t>(group_size.block_length) * group_size.num_in_group;
  if (UNLIKELY(
          group_size.block_length < kTradeEntrySize ||
          remaining < static_cast<size_t>(pos - start) + trades_total_size)) {
    logger.error("TradeEvent: trades group size exceeds buffer");
    return SbeDecoderPolicy::WireMessage{};
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

    trade.price = decode_mantissa(price_mantissa, price_exponent);
    trade.qty = decode_mantissa(qty_mantissa, qty_exponent);

    uint8_t is_buyer_maker;
    std::memcpy(&is_buyer_maker, entry_start + offset, sizeof(uint8_t));

    trade.is_buyer_maker = (is_buyer_maker != 0);
    trade.is_best_match = true;  // Constant value in schema

    pos = entry_start + group_size.block_length;
    event.trades.push_back(std::move(trade));
  }

  pos = parse_varString8(pos, event.symbol);

  return SbeDecoderPolicy::WireMessage{
      std::in_place_type<schema::sbe::SbeTradeEvent>,
      std::move(event)};
}

// Decode BestBidAskStreamEvent (template ID 10001)
// Format: eventTime(8) + bookUpdateId(8) + priceExponent(1) + qtyExponent(1)
//         + bidPrice(8) + bidQty(8) + askPrice(8) + askQty(8) + symbol_varString8
SbeDecoderPolicy::WireMessage decode_best_bid_ask(const char* pos,
    size_t remaining, const common::Logger::Producer& logger) {

  constexpr size_t kMinSize = sizeof(int64_t) * 6 + sizeof(int8_t) * 2;
  if (UNLIKELY(remaining < kMinSize)) {
    logger.error(
        std::format("BestBidAsk: insufficient buffer (need {}, have {})",
            kMinSize,
            remaining));
    return SbeDecoderPolicy::WireMessage{};
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

  event.bid_price = decode_mantissa(bid_price_mantissa, price_exponent);
  event.bid_qty = decode_mantissa(bid_qty_mantissa, qty_exponent);
  event.ask_price = decode_mantissa(ask_price_mantissa, price_exponent);
  event.ask_qty = decode_mantissa(ask_qty_mantissa, qty_exponent);

  pos = parse_varString8(pos, event.symbol);

  return SbeDecoderPolicy::WireMessage{
      std::in_place_type<schema::sbe::SbeBestBidAsk>,
      std::move(event)};
}

// Decode DepthSnapshotStreamEvent (template ID 10002)
// Format: eventTime(8) + bookUpdateId(8) + priceExponent(1) + qtyExponent(1)
//         + bids_group + asks_group + symbol_varString8
SbeDecoderPolicy::WireMessage decode_depth_snapshot(const char* pos,
    size_t remaining, const common::Logger::Producer& logger) {

  constexpr size_t kMinSize =
      sizeof(int64_t) * 2 + sizeof(int8_t) * 2 + sizeof(GroupSize16) * 2;
  if (UNLIKELY(remaining < kMinSize)) {
    logger.error(
        std::format("DepthSnapshot: insufficient buffer (need {}, have {})",
            kMinSize,
            remaining));
    return SbeDecoderPolicy::WireMessage{};
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

  // Bids group
  GroupSize16 bids_group;
  pos = parse_group_header(pos, bids_group);
  const size_t bids_total_size =
      static_cast<size_t>(bids_group.block_length) * bids_group.num_in_group;
  if (UNLIKELY(bids_group.block_length < kLevelSize ||
               remaining < static_cast<size_t>(pos - start) + bids_total_size +
                               sizeof(GroupSize16))) {
    logger.error("DepthSnapshot: bids group size exceeds buffer");
    return SbeDecoderPolicy::WireMessage{};
  }
  event.bids.reserve(bids_group.num_in_group);

  for (uint16_t idx = 0; idx < bids_group.num_in_group; ++idx) {
    const char* const entry_start = pos;
    event.bids.push_back(decode_price_level(pos, price_exponent, qty_exponent));
    pos = entry_start + bids_group.block_length;
  }

  GroupSize16 asks_group;
  pos = parse_group_header(pos, asks_group);
  const size_t asks_total_size =
      static_cast<size_t>(asks_group.block_length) * asks_group.num_in_group;
  if (UNLIKELY(
          asks_group.block_length < kLevelSize ||
          remaining < static_cast<size_t>(pos - start) + asks_total_size)) {
    logger.error("DepthSnapshot: asks group size exceeds buffer");
    return SbeDecoderPolicy::WireMessage{};
  }
  event.asks.reserve(asks_group.num_in_group);

  for (uint16_t i = 0; i < asks_group.num_in_group; ++i) {
    const char* const entry_start = pos;
    event.asks.push_back(decode_price_level(pos, price_exponent, qty_exponent));
    pos = entry_start + asks_group.block_length;
  }

  const auto consumed = static_cast<size_t>(pos - start);
  if (UNLIKELY(remaining < consumed + sizeof(uint8_t))) {
    logger.error("DepthSnapshot: missing symbol length");
    return SbeDecoderPolicy::WireMessage{};
  }
  uint8_t symbol_length;
  std::memcpy(&symbol_length, pos, sizeof(uint8_t));
  if (UNLIKELY(remaining < consumed + sizeof(uint8_t) + symbol_length)) {
    logger.error("DepthSnapshot: symbol exceeds buffer");
    return SbeDecoderPolicy::WireMessage{};
  }

  pos = parse_varString8(pos, event.symbol);

  return SbeDecoderPolicy::WireMessage{
      std::in_place_type<schema::sbe::SbeDepthSnapshot>,
      std::move(event)};
}

// Decode DepthDiffStreamEvent (template ID 10003)
// Format: eventTime(8) + firstBookUpdateId(8) + lastBookUpdateId(8)
//         + priceExponent(1) + qtyExponent(1) + bids_group + asks_group + symbol_varString8
SbeDecoderPolicy::WireMessage decode_depth_diff(const char* pos,
    size_t remaining, const common::Logger::Producer& logger) {

  constexpr size_t kMinSize =
      sizeof(int64_t) * 3 + sizeof(int8_t) * 2 + sizeof(GroupSize16) * 2;
  if (UNLIKELY(remaining < kMinSize)) {
    logger.error(
        std::format("DepthDiff: insufficient buffer (need {}, have {})",
            kMinSize,
            remaining));
    return SbeDecoderPolicy::WireMessage{};
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

  GroupSize16 bids_group;
  pos = parse_group_header(pos, bids_group);
  const size_t bids_total_size =
      static_cast<size_t>(bids_group.block_length) * bids_group.num_in_group;
  if (UNLIKELY(bids_group.block_length < kLevelSize ||
               remaining < static_cast<size_t>(pos - start) + bids_total_size +
                               sizeof(GroupSize16))) {
    logger.error("DepthDiff: bids group size exceeds buffer");
    return SbeDecoderPolicy::WireMessage{};
  }
  event.bids.reserve(bids_group.num_in_group);

  for (uint16_t idx = 0; idx < bids_group.num_in_group; ++idx) {
    const char* const entry_start = pos;
    event.bids.push_back(decode_price_level(pos, price_exponent, qty_exponent));
    pos = entry_start + bids_group.block_length;
  }

  GroupSize16 asks_group;
  pos = parse_group_header(pos, asks_group);
  const size_t asks_total_size =
      static_cast<size_t>(asks_group.block_length) * asks_group.num_in_group;
  if (UNLIKELY(
          asks_group.block_length < kLevelSize ||
          remaining < static_cast<size_t>(pos - start) + asks_total_size)) {
    logger.error("DepthDiff: asks group size exceeds buffer");
    return SbeDecoderPolicy::WireMessage{};
  }
  event.asks.reserve(asks_group.num_in_group);

  for (uint16_t i = 0; i < asks_group.num_in_group; ++i) {
    const char* const entry_start = pos;
    event.asks.push_back(decode_price_level(pos, price_exponent, qty_exponent));
    pos = entry_start + asks_group.block_length;
  }

  const auto consumed = static_cast<size_t>(pos - start);
  if (UNLIKELY(remaining < consumed + sizeof(uint8_t))) {
    logger.error("DepthDiff: missing symbol length");
    return SbeDecoderPolicy::WireMessage{};
  }
  uint8_t symbol_length;
  std::memcpy(&symbol_length, pos, sizeof(uint8_t));
  if (UNLIKELY(remaining < consumed + sizeof(uint8_t) + symbol_length)) {
    logger.error("DepthDiff: symbol exceeds buffer");
    return SbeDecoderPolicy::WireMessage{};
  }

  pos = parse_varString8(pos, event.symbol);

  return SbeDecoderPolicy::WireMessage{
      std::in_place_type<schema::sbe::SbeDepthResponse>,
      std::move(event)};
}

SbeDecoderPolicy::WireMessage try_decode_json_control(std::string_view payload,
    const common::Logger::Producer& logger) {

  if (payload.find("exchangeInfo") != std::string_view::npos) {
    schema::ExchangeInfoResponse exchange;
    auto error_code =
        glz::read<glz::opts{.error_on_unknown_keys = 0}>(exchange, payload);

    if (error_code != glz::error_code::none) {
      const std::string_view view{payload.data(), payload.size()};
      auto msg = glz::format_error(error_code, view);
      logger.error(std::format("Failed to [ExchangeInfo] payload:{}. msg:{}",
          payload,
          msg));
      return SbeDecoderPolicy::WireMessage{};
    }
    return SbeDecoderPolicy::WireMessage{
        std::in_place_type<schema::ExchangeInfoResponse>,
        exchange};
  }

  if (auto api_response = glz::read_json<schema::ApiResponse>(payload)) {
    return SbeDecoderPolicy::WireMessage{
        std::in_place_type<schema::ApiResponse>,
        std::move(*api_response)};
  }

  return SbeDecoderPolicy::WireMessage{};
}

}  // anonymous namespace

SbeDecoderPolicy::WireMessage SbeDecoderPolicy::decode(std::string_view payload,
    const common::Logger::Producer& logger) {

  if (UNLIKELY(payload.empty())) {
    return WireMessage{};
  }

  if (payload == "__CONNECTED__") {
    return WireMessage{};
  }

  if (payload.size() < kHeaderSize) {
    return try_decode_json_control(payload, logger);
  }

  const char* pos = payload.data();
  SBEMessageHeader header;
  std::memcpy(&header, pos, kHeaderSize);
  pos += kHeaderSize;

  const size_t remaining = payload.size() - kHeaderSize;

  switch (header.template_id) {
    case kTradesStreamEventId:  // TradesStreamEvent
      return decode_trade_event(pos, remaining, logger);

    case kBestBidAskStreamEventId:  // BestBidAskStreamEvent
      return decode_best_bid_ask(pos, remaining, logger);

    case kDepthSnapshotStreamEventId:  // DepthSnapshotStreamEvent
      return decode_depth_snapshot(pos, remaining, logger);

    case kDepthDiffStreamEventId:  // DepthDiffStreamEvent
      return decode_depth_diff(pos, remaining, logger);

    default:
      auto json_result = try_decode_json_control(payload, logger);
      if (std::holds_alternative<std::monostate>(json_result)) {
        constexpr int kPayloadMinLength = 200;
        logger.warn(
            "Unknown SBE template ID: {} (schema_id={}, version={}) payload:{}",
            header.template_id,
            header.schema_id,
            header.version,
            payload.substr(0,
                std::min<size_t>(kPayloadMinLength, payload.size())));
      }
      return json_result;
  }
}

}  // namespace core
