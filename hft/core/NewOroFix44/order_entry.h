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
#include <cstdint>

#include "types.h"

namespace trading {
enum class OrderType : char {
  kMarket = '1',
  kLimit = '2',
  kStopLoss = '3',
  kStopLimit = '4',
  kMax
};

enum class TimeInForce : char {
  kDay = '0',
  kGoodTillCancel = '1',
  kImmediateOrCancel = '3',
  kFillOrKill = '4',
};

template <typename T>
constexpr char toChar(T text) noexcept {
  return static_cast<char>(text);
}

enum class Side : char {
  kBuy = '1',  // 매수
  kSell = '2'  // 매도
};

struct NewSingleOrderData {
  std::string cl_order_id;  // Tag 11: 고객 지정 주문 ID (유니크)
  std::string symbol;       // Tag 55: 종목 (예: "BTCUSDT")
  char handl_inst = '1';
  Side side;           // Tag 54: 매매 방향 ('1' = Buy, '2' = Sell)
  double order_qty;    // Tag 38: 주문 수량
  OrderType ord_type;  // Tag 40: 주문 유형 ('1' = Market, '2' = Limit)
  double price;        // Tag 44: 지정가 (Limit 주문일 때만 사용)
  TimeInForce
      time_in_force;  // Tag 59: 주문 유효 기간 ('0' = Day, '1' = GTC 등)
  std::string transactTime;  // Tag 60: 전송 시간 (YYYYMMDD‑HH:MM:SS.sss)
};

// enum class 정의 (필요시)
enum class ExecType : char {
  kNew = '0',
  kPartialFill = '1',
  kFill = '2',
  kDoneForDay = '3',
  kCanceled = '4',
  kReplaced = '5',
  kPendingCancel = '6',
  kStopped = '7',
  kRejected = '8',
  kSuspended = '9',
  kPendingNew = 'A',
  kCalculated = 'B',
  kExpired = 'C',
  kRestated = 'D',
  kPendingReplace = 'E'
};

inline const char* toString(ExecType execType) {
  switch (execType) {
    case ExecType::kNew:
      return "New";
    case ExecType::kPartialFill:
      return "PartialFill";
    case ExecType::kFill:
      return "Fill";
    case ExecType::kDoneForDay:
      return "DoneForDay";
    case ExecType::kCanceled:
      return "Canceled";
    case ExecType::kReplaced:
      return "Replaced";
    case ExecType::kPendingCancel:
      return "PendingCancel";
    case ExecType::kStopped:
      return "Stopped";
    case ExecType::kRejected:
      return "Rejected";
    case ExecType::kSuspended:
      return "Suspended";
    case ExecType::kPendingNew:
      return "PendingNew";
    case ExecType::kCalculated:
      return "Calculated";
    case ExecType::kExpired:
      return "Expired";
    case ExecType::kRestated:
      return "Restated";
    case ExecType::kPendingReplace:
      return "PendingReplace";
    default:
      return "Unknown";
  }
}

enum class OrdStatus : char {
  kNew = '0',
  kPartiallyFilled = '1',
  kFilled = '2',
  kDoneForDay = '3',
  kCanceled = '4',
  kReplaced = '5',
  kPendingCancel = '6',
  kStopped = '7',
  kRejected = '8',
  kSuspended = '9',
  kPendingNew = 'A',
  kCalculated = 'B',
  kExpired = 'C'
};

inline const char* toString(OrdStatus status) {
  switch (status) {
    case OrdStatus::kNew:
      return "New";
    case OrdStatus::kPartiallyFilled:
      return "PartiallyFilled";
    case OrdStatus::kFilled:
      return "Filled";
    case OrdStatus::kDoneForDay:
      return "DoneForDay";
    case OrdStatus::kCanceled:
      return "Canceled";
    case OrdStatus::kReplaced:
      return "Replaced";
    case OrdStatus::kPendingCancel:
      return "PendingCancel";
    case OrdStatus::kStopped:
      return "Stopped";
    case OrdStatus::kRejected:
      return "Rejected";
    case OrdStatus::kSuspended:
      return "Suspended";
    case OrdStatus::kPendingNew:
      return "PendingNew";
    case OrdStatus::kCalculated:
      return "Calculated";
    case OrdStatus::kExpired:
      return "Expired";
    default:
      return "Unknown";
  }
}

inline ExecType execTypeFromChar(char text) {
  return static_cast<ExecType>(text);
}
inline OrdStatus ordStatusFromChar(char text) {
  return static_cast<OrdStatus>(text);
}

struct ExecutionReport {
  std::string cl_ord_id;
  int order_id;
  std::string symbol;
  ExecType exec_type;
  OrdStatus ord_status;
  double cum_qty;
  double leaves_qty;
  double last_qty;
  int reason;
  double price;
  std::string text;
};
}  // namespace trading