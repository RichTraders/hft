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

#ifndef EXECUTION_REPORT_H
#define EXECUTION_REPORT_H
#include "types.h"

namespace trading {
enum class OrderType : uint8_t {
  kMarket = 1,
  kLimit = 2,
  kStopLoss = 3,
  kStopLimit = 4,
  kMax
};

enum class TimeInForce : uint8_t {
  kGoodTillCancel = 1,
  kImmediateOrCancel = 3,
  kFillOrKill = 4,
};

enum class SelfTradePreventionMode : uint8_t {
  kNone = 1,
  kExpireTaker = 2,
  kExpireMaker = 3,
  kExpireBoth = 4,
  kDecrement = 5,
};

inline const char* toString(OrderType type) {
  switch (type) {
    case OrderType::kMarket:
      return "Market";
    case OrderType::kLimit:
      return "Limit";
    case OrderType::kStopLoss:
      return "StopLoss";
    case OrderType::kStopLimit:
      return "StopLimit";
    case OrderType::kMax:
      return "Max";
    default:
      return "Unknown";
  }
}

inline const char* toString(TimeInForce tif) {
  switch (tif) {
    case TimeInForce::kGoodTillCancel:
      return "GTC";
    case TimeInForce::kImmediateOrCancel:
      return "IOC";
    case TimeInForce::kFillOrKill:
      return "FOK";
    default:
      return "Unknown";
  }
}

inline const char* toString(SelfTradePreventionMode mode) {
  switch (mode) {
    case SelfTradePreventionMode::kNone:
      return "None";
    case SelfTradePreventionMode::kExpireTaker:
      return "ExpireTaker";
    case SelfTradePreventionMode::kExpireMaker:
      return "ExpireMaker";
    case SelfTradePreventionMode::kExpireBoth:
      return "ExpireBoth";
    case SelfTradePreventionMode::kDecrement:
      return "Decrement";
    default:
      return "Unknown";
  }
}

struct NewOrder {
  std::string id;  //ClOrdID
  OrderType order_type;
  common::Qty order_qty;
  common::Price price;
  common::Side side;
};

enum class OrderStatus : uint8_t {
  kNew = 1,
  kPartiallyFilled = 2,
  kFilled = 3,
  kCancelled = 4,
  kPendingCancelled = 6,
  kRejected = 8,
  kPendingNew = 0xA,
  kExpired = 0xC,
};

inline const char* toString(OrderStatus status) {
  switch (status) {
    case OrderStatus::kNew:
      return "New";
    case OrderStatus::kPartiallyFilled:
      return "PartiallyFilled";
    case OrderStatus::kFilled:
      return "Filled";
    case OrderStatus::kCancelled:
      return "Cancelled";
    case OrderStatus::kPendingCancelled:
      return "PendingCancelled";
    case OrderStatus::kRejected:
      return "Rejected";
    case OrderStatus::kPendingNew:
      return "PendingNew";
    case OrderStatus::kExpired:
      return "Expired";
    default:
      return "Unknown";
  }
}

struct ExecutionReport {
  std::string execution_id;
  int order_id;
  common::Price price;
  common::Qty qty;
  common::Side side;
  std::string symbol;
  OrderStatus order_status;
  common::Price last_price;
  common::Qty last_qty;
  std::string trade_id;

  [[nodiscard]] std::string toString() const {
    std::ostringstream stream;
    stream << "ExecutionReport{" << "execution_id='" << execution_id << "', "
           << "order_id=" << order_id << ", " << "price=" << price.value << ", "
           << "qty=" << qty.value << ", " << "side=" << common::toString(side)
           << ", " << "symbol='" << symbol << "', "
           << "order_status=" << trading::toString(order_status) << ", "
           << "last_price=" << last_price.value << ", "
           << "last_qty=" << last_qty.value << ", " << "trade_id='" << trade_id
           << "'" << "}";
    return stream.str();
  }
};
}  // namespace trading
#endif  //EXECUTION_REPORT_H