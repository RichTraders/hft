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

#ifndef ORDER_REQUEST_H
#define ORDER_REQUEST_H

#include "order_entry.h"

enum class ReqeustType : uint8_t {
  kInvalid = 0,
  kNewSingleOrderData = 1,
  kOrderCancelRequest = 2,
  kOrderCancelRequestAndNewOrderSingle = 3,
  kOrderMassCancelRequest = 4,
};

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

struct RequestCommon {
  ReqeustType req_type{ReqeustType::kInvalid};
  common::OrderId cl_order_id{common::OrderId{common::kOrderIdInvalid}};
  std::string symbol{"BTCUSDT"};
  common::Side side{common::Side::kInvalid};
  float order_qty{0.};
  trading::OrderType ord_type{trading::OrderType::kInvalid};
  float price{0.};
  trading::TimeInForce time_in_force{trading::TimeInForce::kInvalid};
  trading::SelfTradePreventionMode self_trade_prevention_mode{
      trading::SelfTradePreventionMode::kExpireTaker};

  [[nodiscard]] std::string toString() const {
    std::ostringstream oss;
    oss << "RequestCommon{" << "cl_order_id=" << cl_order_id.value
        << ", symbol=" << symbol << ", side=" << common::toString(side)
        << ", order_qty=" << order_qty
        << ", ord_type=" << trading::toString(ord_type) << ", price=" << price
        << ", time_in_force=" << trading::toString(time_in_force)
        << ", self_trade_prevention_mode="
        << trading::toString(self_trade_prevention_mode) << "}";
    return oss.str();
  }
};
#endif  //ORDER_REQUEST_H