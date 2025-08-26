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

#pragma once
#include "types.h"

namespace trading {

enum class ReqeustType : uint8_t {
  kInvalid = 0,
  kNewSingleOrderData = 1,
  kOrderCancelRequest = 2,
  kOrderCancelRequestAndNewOrderSingle = 3,
  kOrderMassCancelRequest = 4,
};

enum class ResponseType : uint8_t {
  kInvalid = 0,
  kExecutionReport = 1,
  kOrderCancelReject = 2,
  kOrderMassCancelReport = 3
};

enum class OrderType : char {
  kInvalid = '0',
  kMarket = '1',
  kLimit = '2',
  kStopLoss = '3',
  kStopLimit = '4',
};

inline std::string toString(OrderType type) {
  switch (type) {
    case OrderType::kMarket:
      return "Market";
    case OrderType::kLimit:
      return "Limit";
    case OrderType::kStopLoss:
      return "StopLoss";
    case OrderType::kStopLimit:
      return "StopLimit";
    default:
      return "Unknown";
  }
}

enum class TimeInForce : char {
  kInvalid = '0',
  kGoodTillCancel = '1',
  kImmediateOrCancel = '3',
  kFillOrKill = '4',
};

inline std::string toString(TimeInForce time_in_force) {
  switch (time_in_force) {
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

template <typename T>
constexpr char to_char(T text) noexcept {
  return static_cast<char>(text);
}

enum class OrderSide : char {
  kNone = '0',
  kBuy = '1',  // 매수
  kSell = '2'  // 매도
};

inline std::string toString(OrderSide side) {
  switch (side) {
    case OrderSide::kBuy:
      return "Buy";
    case OrderSide::kSell:
      return "Sell";
    default:
      return "Unknown";
  }
}

enum class SelfTradePreventionMode : char {
  kNone = '1',
  kExpireTaker = '2',
  kExpireMaker = '3',
  kExpireBoth = '4',
  kDecrement = '5',
};

inline std::string toString(SelfTradePreventionMode mode) {
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

struct NewSingleOrderData {
  common::OrderId cl_order_id = common::OrderId(common::kOrderIdInvalid);
  std::string symbol;  // Tag 55: 종목 (예: "BTCUSDT")
  OrderSide side;      // Tag 54: 매매 방향 ('1' = Buy, '2' = Sell)
  common::Qty order_qty = common::Qty{.0f};
  OrderType ord_type;  // Tag 40: 주문 유형 ('1' = Market, '2' = Limit)
  common::Price price = common::Price{.0f};
  TimeInForce
      time_in_force;  // Tag 59: 주문 유효 기간 ('0' = Day, '1' = GTC 등)
  SelfTradePreventionMode self_trade_prevention_mode =
      SelfTradePreventionMode::kExpireTaker;
};

enum class ExecType : char {
  kNew = '0',
  kCanceled = '4',
  kReplaced = '5',
  kRejected = '8',
  kSuspended = '9',
  kTrade = 'F',
  kExpired = 'C',
};

enum class OrdStatus : char {
  kNew = '0',
  kPartiallyFilled = '1',
  kFilled = '2',
  kCanceled = '4',
  kPendingCancel = '6',
  kRejected = '8',
  kPendingNew = 'A',
  kExpired = 'C'
};

enum class MassCancelResponse : char {
  kCancelRequestRejected = '0',
  kCancelSymbolOrders = '1'
};

inline const char* toString(ExecType execType) {
  switch (execType) {
    case ExecType::kNew:
      return "New";
    case ExecType::kCanceled:
      return "Canceled";
    case ExecType::kReplaced:
      return "Replaced";
    case ExecType::kRejected:
      return "Rejected";
    case ExecType::kSuspended:
      return "Suspended";
    case ExecType::kTrade:
      return "Trade";
    case ExecType::kExpired:
      return "Expired";
    default:
      return "Unknown";
  }
}

inline const char* toString(OrdStatus status) {
  switch (status) {
    case OrdStatus::kNew:
      return "New";
    case OrdStatus::kPartiallyFilled:
      return "PartiallyFilled";
    case OrdStatus::kFilled:
      return "Filled";
    case OrdStatus::kCanceled:
      return "Canceled";
    case OrdStatus::kPendingCancel:
      return "PendingCancel";
    case OrdStatus::kRejected:
      return "Rejected";
    case OrdStatus::kPendingNew:
      return "PendingNew";
    case OrdStatus::kExpired:
      return "Expired";
    default:
      return "Unknown";
  }
}

inline auto toString(ReqeustType type) -> std::string {
  switch (type) {
    case ReqeustType::kNewSingleOrderData:
      return "Order";
    case ReqeustType::kOrderCancelRequest:
      return "Cancel";
    default:
      return "Unknown";
  }
}

inline ExecType exec_type_from_char(char text) {
  return static_cast<ExecType>(text);
}

inline OrdStatus ord_status_from_char(char text) {
  return static_cast<OrdStatus>(text);
}

inline MassCancelResponse mass_cancel_response_from_char(char text) {
  return static_cast<MassCancelResponse>(text);
}

inline OrderSide to_common_side(common::Side side) noexcept {
  switch (side) {
    case common::Side::kBuy:
      return OrderSide::kBuy;
    case common::Side::kSell:
      return OrderSide::kSell;
    default:
      return OrderSide::kNone;
  }
}

struct RequestCommon {
  ReqeustType req_type{ReqeustType::kInvalid};
  common::OrderId cl_order_id{common::OrderId{common::kOrderIdInvalid}};
  common::OrderId orig_cl_order_id{
      common::OrderId{common::kOrderIdInvalid}};  // 주문 취소할때 사용
  std::string symbol{"BTCUSDT"};
  common::Side side{common::Side::kInvalid};
  common::Qty order_qty{0.};
  OrderType ord_type{OrderType::kInvalid};
  common::Price price{0.};
  TimeInForce time_in_force{TimeInForce::kInvalid};
  SelfTradePreventionMode self_trade_prevention_mode{
      SelfTradePreventionMode::kExpireTaker};

  [[nodiscard]] std::string toString() const {
    std::ostringstream oss;
    oss << "RequestCommon{" << "cl_order_id=" << cl_order_id.value
        << ", symbol=" << symbol << ", side=" << common::toString(side)
        << ", order_qty=" << order_qty.value
        << ", ord_type=" << trading::toString(ord_type)
        << ", price=" << price.value
        << ", time_in_force=" << trading::toString(time_in_force)
        << ", self_trade_prevention_mode="
        << trading::toString(self_trade_prevention_mode) << "}";
    return oss.str();
  }
};

struct OrderCancelRequest {
  common::OrderId cl_order_id;
  common::OrderId orig_cl_order_id;
  std::string symbol;
};

struct OrderCancelRequestAndNewOrderSingle {
  int order_cancel_request_and_new_order_single_mode = 1;
  common::OrderId cancel_order_id;
  common::OrderId cl_order_id;
  std::string symbol;
  OrderSide side;
  common::Qty order_qty;
  OrderType ord_type;
  common::Price price;
  TimeInForce time_in_force;
  SelfTradePreventionMode self_trade_prevention_mode =
      SelfTradePreventionMode::kExpireTaker;
};

struct OrderMassCancelRequest {
  common::OrderId cl_order_id;
  std::string symbol;
  char mass_cancel_request_type = '1';
};

struct ExecutionReport {
  common::OrderId cl_order_id = common::OrderId(common::kOrderIdInvalid);
  std::string symbol;
  ExecType exec_type;
  OrdStatus ord_status;
  common::Qty cum_qty = common::Qty{.0f};
  common::Qty leaves_qty = common::Qty{.0f};
  common::Qty last_qty = common::Qty{.0f};
  int error_code;
  common::Price price = common::Price{.0f};
  common::Side side;

  [[nodiscard]] std::string toString() const {
    std::ostringstream stream;
    stream << "ExecutionReport{" << "order_id=" << cl_order_id.value
           << ", symbol=" << symbol
           << ", exec_type=" << trading::toString(exec_type)
           << ", ord_status=" << trading::toString(ord_status)
           << ", cum_qty=" << cum_qty.value
           << ", leaves_qty=" << leaves_qty.value
           << ", last_qty=" << last_qty.value << ", error_code=" << error_code
           << ", price=" << price.value << ", side=" << common::toString(side)
           << "}";
    return stream.str();
  }
};

struct OrderCancelReject {
  common::OrderId cl_order_id = common::OrderId(common::kOrderIdInvalid);
  std::string symbol;
  int error_code;
};

struct OrderMassCancelReport {
  common::OrderId cl_order_id = common::OrderId(common::kOrderIdInvalid);
  std::string symbol;
  char mass_cancel_request_type;
  MassCancelResponse mass_cancel_response;
  int total_affected_orders;
  int error_code;
};

struct ResponseCommon {
  ResponseType res_type{ResponseType::kInvalid};

  ExecutionReport* execution_report = nullptr;
  OrderCancelReject* order_cancel_reject = nullptr;
  OrderMassCancelReport* order_mass_cancel_report = nullptr;
};
// TODO(CH) struct OrderAmendKeepPriorityRequest {}; 차후에 필요할지도

struct OrderReject {
  std::string session_reject_reason;
  int rejected_message_type;
  std::string error_message;
  int error_code;
  [[nodiscard]] std::string toString() const {
    std::ostringstream stream;
    stream << "OrderReject{"
           << "session_reject_reason=" << session_reject_reason
           << ", rejected_message_type=" << rejected_message_type
           << ", error_code=" << error_code
           << ", error_message=" << std::quoted(error_message) << "}";
    return stream.str();
  }
};
}  // namespace trading