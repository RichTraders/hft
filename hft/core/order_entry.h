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

#ifndef ORDER_ENTRY_H
#define ORDER_ENTRY_H
#include <common/types.h>

#include "common/precision_config.hpp"

namespace trading {

enum class ReqeustType : uint8_t {
  kInvalid = 0,
  kNewSingleOrderData = 1,
  kOrderCancelRequest = 2,
  kOrderCancelRequestAndNewOrderSingle = 3,
  kOrderModify = 4,
  kOrderMassCancelRequest = 5,
};

inline auto toString(ReqeustType type) -> std::string {
  switch (type) {
    case ReqeustType::kNewSingleOrderData:
      return "Order";
    case ReqeustType::kOrderCancelRequest:
      return "Cancel";
    case ReqeustType::kOrderModify:
      return "Modify";
    default:
      return "Unknown";
  }
}

enum class ResponseType : uint8_t {
  kInvalid = 0,
  kExecutionReport = 1,
  kOrderCancelReject = 2,
  kOrderMassCancelReport = 3
};

inline std::string toString(ResponseType type) {
  switch (type) {
    case ResponseType::kInvalid:
      return "INVALID";
    case ResponseType::kExecutionReport:
      return "EXECUTION_REPORT";
    case ResponseType::kOrderCancelReject:
      return "ORDER_CANCEL_REJECT";
    case ResponseType::kOrderMassCancelReport:
      return "ORDER_MASS_CANCEL_REPORT";
    default:
      return "Unknown";
  }
}

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
      return "MARKET";
    case OrderType::kLimit:
      return "LIMIT";
    case OrderType::kStopLoss:
      return "STOP_LOSS";
    case OrderType::kStopLimit:
      return "STOP_LIMIT";
    default:
      return "UNKNOWN";
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
      return "UNKNOWN";
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
      return "BUY";
    case OrderSide::kSell:
      return "SELL";
    default:
      return "UNKNOWN";
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
      return "NONE";
    case SelfTradePreventionMode::kExpireTaker:
      return "EXPIRE_TAKER";
    case SelfTradePreventionMode::kExpireMaker:
      return "EXPIRE_MAKER";
    case SelfTradePreventionMode::kExpireBoth:
      return "EXPIRE_BOTH";
    case SelfTradePreventionMode::kDecrement:
      return "DECREMENT";
    default:
      return "UNKNOWN";
  }
}

struct NewSingleOrderData {
  common::OrderId cl_order_id;
  std::string symbol;
  OrderSide side;
  common::Qty order_qty;
  OrderType ord_type;
  common::Price price;
  TimeInForce time_in_force;
  SelfTradePreventionMode self_trade_prevention_mode =
      SelfTradePreventionMode::kExpireTaker;
  std::optional<common::PositionSide> position_side;  // For futures trading
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

inline const char* toString(ExecType execType) {
  switch (execType) {
    case ExecType::kNew:
      return "NEW";
    case ExecType::kCanceled:
      return "CANCELED";
    case ExecType::kReplaced:
      return "REPLACED";
    case ExecType::kRejected:
      return "REJECTED";
    case ExecType::kSuspended:
      return "SUSPENDED";
    case ExecType::kTrade:
      return "TRADE";
    case ExecType::kExpired:
      return "EXPIRED";
    default:
      return "UNKNOWN";
  }
}

inline ExecType toType(std::string_view type) {
  if (type == "NEW") {
    return ExecType::kNew;
  }
  if (type == "TRADE") {
    return ExecType::kTrade;
  }
  if (type == "CANCELED") {
    return ExecType::kCanceled;
  }
  if (type == "REJECTED") {
    return ExecType::kRejected;
  }
  return ExecType::kNew;
}

enum class OrdStatus : char {
  kInvalid = 0,
  kNew = '0',
  kPartiallyFilled = '1',
  kFilled = '2',
  kCanceled = '4',
  kPendingCancel = '6',
  kRejected = '8',
  kPendingNew = 'A',
  kExpired = 'C'
};

inline const char* toString(OrdStatus status) {
  switch (status) {
    case OrdStatus::kInvalid:
      return "INVALID";
    case OrdStatus::kNew:
      return "NEW";
    case OrdStatus::kPartiallyFilled:
      return "PARIALLY_FILLED";
    case OrdStatus::kFilled:
      return "FILLED";
    case OrdStatus::kCanceled:
      return "CANCELED";
    case OrdStatus::kPendingCancel:
      return "PENDING_CANCEL";
    case OrdStatus::kRejected:
      return "REJECTED";
    case OrdStatus::kPendingNew:
      return "PENDING_NEW";
    case OrdStatus::kExpired:
      return "EXPIRED";
    default:
      return "UNKNOWN";
  }
}

inline OrdStatus toOrderStatus(std::string_view status) {
  if (status == "NEW") {
    return OrdStatus::kNew;
  }
  if (status == "PARTIALLY_FILLED") {
    return OrdStatus::kPartiallyFilled;
  }
  if (status == "FILLED") {
    return OrdStatus::kFilled;
  }
  if (status == "CANCELED") {
    return OrdStatus::kCanceled;
  }
  if (status == "PENDING_CANCELED") {
    return OrdStatus::kPendingCancel;
  }
  if (status == "REJECTED") {
    return OrdStatus::kRejected;
  }
  if (status == "PENDING_NEW") {
    return OrdStatus::kPendingNew;
  }
  if (status == "EXPIRED") {
    return OrdStatus::kExpired;
  }
  return OrdStatus::kInvalid;
}

enum class MassCancelResponse : char {
  kCancelRequestRejected = '0',
  kCancelSymbolOrders = '1'
};

inline std::string toString(MassCancelResponse response) {
  switch (response) {
    case MassCancelResponse::kCancelRequestRejected:
      return "CANCEL_REJECTED";
    case MassCancelResponse::kCancelSymbolOrders:
      return "CANCEL_SYMBOL_ORDERS";
    default:
      return "UNKNOWN";
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

inline OrderSide from_common_side(common::Side side) noexcept {
  switch (side) {
    case common::Side::kBuy:
      return OrderSide::kBuy;
    case common::Side::kSell:
      return OrderSide::kSell;
    default:
      return OrderSide::kNone;
  }
}

inline common::Side to_common_side(OrderSide side) noexcept {
  switch (side) {
    case OrderSide::kBuy:
      return common::Side::kBuy;
    case OrderSide::kSell:
      return common::Side::kSell;
    default:
      return common::Side::kInvalid;
  }
}

struct RequestCommon {
  ReqeustType req_type{ReqeustType::kInvalid};
  common::OrderId cl_cancel_order_id{common::OrderId{common::kOrderIdInvalid}};
  common::OrderId cl_order_id{common::OrderId{common::kOrderIdInvalid}};
  common::OrderId orig_cl_order_id{common::OrderId{common::kOrderIdInvalid}};
  std::string symbol{"BTCUSDT"};
  common::Side side{common::Side::kInvalid};
  common::Qty order_qty{0.};
  OrderType ord_type{OrderType::kInvalid};
  common::Price price{0.};
  TimeInForce time_in_force{TimeInForce::kInvalid};
  SelfTradePreventionMode self_trade_prevention_mode{
      SelfTradePreventionMode::kExpireTaker};
  std::optional<common::PositionSide> position_side{std::nullopt};

  [[nodiscard]] std::string toString() const {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(PRECISION_CONFIG.qty_precision());
    stream << "RequestCommon{"
           << "cl_cancel_order_id=" << cl_cancel_order_id.value
           << ", cl_order_id=" << cl_order_id.value
           << ", orig_cl_order_id=" << orig_cl_order_id.value
           << ", symbol=" << symbol << ", side=" << common::toString(side)
           << ", order_qty=" << order_qty.value
           << ", ord_type=" << trading::toString(ord_type)
           << ", price=" << price.value
           << ", time_in_force=" << trading::toString(time_in_force)
           << ", self_trade_prevention_mode="
           << trading::toString(self_trade_prevention_mode);
    if (position_side) {
      stream << ", position_side=" << common::toString(*position_side);
    }
    stream << "}";
    return stream.str();
  }
};

struct OrderCancelRequest {
  common::OrderId cl_order_id;
  common::OrderId orig_cl_order_id;
  std::string symbol;
  std::optional<common::PositionSide> position_side;
};

struct OrderCancelAndNewOrderSingle {
  int order_cancel_request_and_new_order_single_mode = 1;
  common::OrderId cancel_new_order_id;
  common::OrderId cl_new_order_id;
  common::OrderId cl_origin_order_id;
  std::string symbol;
  OrderSide side;
  common::Qty order_qty;
  OrderType ord_type;
  common::Price price;
  TimeInForce time_in_force;
  SelfTradePreventionMode self_trade_prevention_mode =
      SelfTradePreventionMode::kExpireTaker;
  std::optional<common::PositionSide> position_side;
};

struct OrderMassCancelRequest {
  common::OrderId cl_order_id;
  std::string symbol;
  char mass_cancel_request_type = '1';
};

struct OrderModifyRequest {
  common::OrderId orig_client_order_id;
  std::string symbol;
  OrderSide side;
  common::Price price;
  common::Qty order_qty;
  std::optional<common::PositionSide> position_side;
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
  std::string text;
  std::optional<common::PositionSide> position_side;  // For futures trading
  bool is_maker = false;                              // taker=false, maker=true

  [[nodiscard]] std::string toString() const {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(PRECISION_CONFIG.qty_precision());
    stream << "ExecutionReport{" << "order_id=" << cl_order_id.value
           << ", symbol=" << symbol
           << ", exec_type=" << trading::toString(exec_type)
           << ", ord_status=" << trading::toString(ord_status)
           << ", cum_qty=" << cum_qty.value
           << ", leaves_qty=" << leaves_qty.value
           << ", last_qty=" << last_qty.value << ", error_code=" << error_code
           << ", price=" << price.value << ", side=" << common::toString(side)
           << ", text=" << text;
    if (position_side) {
      stream << ", position_side=" << common::toString(*position_side);
    }
    stream << ", is_maker=" << (is_maker ? "true" : "false");
    stream << "}";
    return stream.str();
  }
};

struct OrderCancelReject {
  common::OrderId cl_order_id = common::OrderId(common::kOrderIdInvalid);
  std::string symbol;
  int error_code;
  std::string text;

  [[nodiscard]] std::string toString() const {
    std::ostringstream stream;
    stream << "ExecutionReport{" << "order_id=" << cl_order_id.value
           << ", symbol=" << symbol << ", error_code=" << error_code
           << ", text=" << text << "}";
    return stream.str();
  }
};

struct OrderMassCancelReport {
  common::OrderId cl_order_id = common::OrderId(common::kOrderIdInvalid);
  std::string symbol;
  char mass_cancel_request_type;
  MassCancelResponse mass_cancel_response;
  int total_affected_orders;
  int error_code;
  std::string text;

  [[nodiscard]] std::string toString() const {
    std::ostringstream stream;
    stream << "ExecutionReport{" << "order_id=" << cl_order_id.value
           << ", symbol=" << symbol
           << ", mass_cancel_request_type=" << mass_cancel_request_type
           << ", mass_cancel_response="
           << trading::toString(mass_cancel_response)
           << ", total_affected_orders=" << total_affected_orders
           << ", error_code=" << error_code << ", text=" << text << "}";
    return stream.str();
  }
};

struct OrderAmendReject {};

struct ResponseCommon {
  ResponseType res_type{ResponseType::kInvalid};

  ExecutionReport* execution_report = nullptr;
  OrderCancelReject* order_cancel_reject = nullptr;
  OrderMassCancelReport* order_mass_cancel_report = nullptr;
  OrderAmendReject* order_amend_reject = nullptr;
};
// TODO(CH) struct OrderAmendKeepPriorityRequest {}; 차후에 필요할지도

struct OrderReject {
  std::string session_reject_reason;
  int rejected_message_type{-1};
  std::string error_message;
  int error_code{-1};
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

struct OrderMessage {
  enum class Type : uint8_t {
    kNewOrder,
    kCancel,
    kReplace,
    kExecutionReport,
    kOrderAck
  };

  Type type{Type::kNewOrder};

  std::string cl_order_id;
  std::string symbol;
  common::Side side{common::Side::kInvalid};
  trading::OrderType order_type{trading::OrderType::kLimit};
  trading::TimeInForce time_in_force{trading::TimeInForce::kGoodTillCancel};

  double price{0.0};
  double qty{0.0};
  double stop_price{0.0};

  std::optional<std::string> orig_cl_order_id;

  std::optional<int64_t> order_id;
  std::optional<std::string> exec_id;
  std::optional<trading::OrdStatus> order_status;
  std::optional<double> executed_qty;
  std::optional<double> cumulative_quote_qty;
  std::optional<uint64_t> transact_time;
  std::optional<int64_t> strategy_id;
};

}  // namespace trading

namespace core {
using OrderMessage = trading::OrderMessage;
}  // namespace core

#endif