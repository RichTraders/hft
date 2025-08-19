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

#ifndef OM_ORDER_H
#define OM_ORDER_H

#include "types.h"

namespace trading {
enum class OMOrderState : int8_t {
  kInvalid = 0,
  kPendingNew = 1,
  kLive = 2,
  kPendingCancel = 3,
  kDead = 4
};

inline auto toString(OMOrderState side) -> std::string {
  switch (side) {
    case OMOrderState::kPendingNew:
      return "PENDING_NEW";
    case OMOrderState::kLive:
      return "LIVE";
    case OMOrderState::kPendingCancel:
      return "PENDING_CANCEL";
    case OMOrderState::kDead:
      return "DEAD";
    case OMOrderState::kInvalid:
      return "INVALID";
  }

  return "UNKNOWN";
}

struct Order {
  Order() = default;

  Order(common::TickerId ticker_id, const common::OrderId order,
        const common::Side side, const common::Price price,
        const common::Qty qty, const OMOrderState state)
      : ticker_id(std::move(ticker_id)),
        order_id(order),
        side(side),
        price(price),
        qty(qty),
        order_state(state) {}

  common::TickerId ticker_id;
  common::OrderId order_id{common::OrderId{common::kOrderIdInvalid}};
  common::Side side{common::Side::kInvalid};
  common::Price price{common::kPriceInvalid};
  common::Qty qty{common::kQtyInvalid};
  OMOrderState order_state{OMOrderState::kInvalid};

  [[nodiscard]] auto toString() const {
    std::ostringstream stream;
    stream << "OMOrder" << "[" << "tid:" << ticker_id << " "
           << "oid:" << common::toString(order_id) << " "
           << "side:" << common::toString(side) << " "
           << "price:" << common::toString(price) << " "
           << "qty:" << common::toString(qty) << " "
           << "state:" << trading::toString(order_state) << "]";

    return stream.str();
  }
};

constexpr std::size_t kSlotsPerSide = 8;
using OMOrderSideHashMap =
    std::array<std::array<Order, kSlotsPerSide>,
               common::sideToIndex(common::Side::kTrade)>;

using OMOrderTickerSideHashMap =
    absl::flat_hash_map<std::string, OMOrderSideHashMap>;
}  // namespace trading

#endif  //OM_ORDER_H