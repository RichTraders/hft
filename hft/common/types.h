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

#ifndef TYPES_H
#define TYPES_H

namespace common {
using OrderId = uint64_t;
constexpr auto kOrderIdInvalid = std::numeric_limits<OrderId>::max();

inline auto orderIdToString(OrderId order_id) -> std::string {
  if (UNLIKELY(order_id == kOrderIdInvalid)) {
    return "INVALID";
  }

  return std::to_string(order_id);
}

using TickerId = const std::string;
constexpr auto kTickerIdInvalid = "";

using ClientId = uint32_t;
constexpr auto kClientIdInvalid = std::numeric_limits<ClientId>::max();

inline auto clientIdToString(ClientId client_id) -> std::string {
  if (UNLIKELY(client_id == kClientIdInvalid)) {
    return "INVALID";
  }

  return std::to_string(client_id);
}

using Price = float;
constexpr auto kPriceInvalid = std::numeric_limits<Price>::max();

inline auto priceToString(Price price) -> std::string {
  if (UNLIKELY(price == kPriceInvalid)) {
    return "INVALID";
  }

  return std::to_string(price);
}

using Qty = float;
constexpr auto kQtyInvalid = std::numeric_limits<Qty>::max();

inline auto qtyToString(Qty qty) -> std::string {
  if (UNLIKELY(qty == kQtyInvalid)) {
    return "INVALID";
  }

  return std::to_string(qty);
}

using Priority = uint64_t;
constexpr auto kPriorityInvalid = std::numeric_limits<Priority>::max();

inline auto priorityToString(Priority priority) -> std::string {
  if (UNLIKELY(priority == kPriorityInvalid)) {
    return "INVALID";
  }

  return std::to_string(priority);
}

enum class Side : char {
  kInvalid = '\0',
  kBuy = '0',
  kSell = '1',
  kTrade = '2',
};

inline auto sideToString(const Side side) -> std::string {
  switch (side) {
    case Side::kBuy:
      return "BUY";
    case Side::kSell:
      return "SELL";
    case Side::kInvalid:
      return "INVALID";
    case Side::kTrade:
      return "TRADE";
  }

  return "UNKNOWN";
}

inline Side charToSide(const char character) {
  switch (character) {
    case '0':
      return Side::kBuy;
    case '1':
      return Side::kSell;
    case '2':
      return Side::kTrade;
    default:
      return Side::kInvalid;
  }
}

constexpr auto sideToIndex(Side side) noexcept {
  return static_cast<size_t>(side) + 1;
}

constexpr auto sideToValue(Side side) noexcept {
  return static_cast<int>(side);
}

enum class MarketUpdateType : uint8_t {
  kInvalid = 0,
  kClear = 1,
  kAdd = 2,
  kModify = 3,
  kCancel = 4,
  kTrade = 5,
};

inline MarketUpdateType charToMarketUpdateType(const char character) {
  switch (character) {
    case '0':
      return MarketUpdateType::kAdd;
    case '1':
      return MarketUpdateType::kModify;
    case '2':
      return MarketUpdateType::kCancel;
    default:
      return MarketUpdateType::kInvalid;
  }
}

inline std::string marketUpdateTypeToString(MarketUpdateType type) {
  switch (type) {
    case MarketUpdateType::kClear:
      return "CLEAR";
    case MarketUpdateType::kAdd:
      return "ADD";
    case MarketUpdateType::kModify:
      return "MODIFY";
    case MarketUpdateType::kCancel:
      return "CANCEL";
    case MarketUpdateType::kTrade:
      return "TRADE";
    case MarketUpdateType::kInvalid:
      return "INVALID";
  }
  return "UNKNOWN";
}
}  // namespace common
#endif  //TYPES_H