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

#ifndef MARKET_DATA_H
#define MARKET_DATA_H

#include "types.h"

struct MarketData {
  common::MarketUpdateType type = common::MarketUpdateType::kInvalid;
  common::OrderId order_id = common::kOrderIdInvalid;
  common::TickerId ticker_id = common::kTickerIdInvalid;
  common::Side side = common::Side::kInvalid;
  common::Price price = common::kPriceInvalid;
  common::Qty qty = common::kQtyInvalid;

  //For MemoryPool allocation
  MarketData() = default;

  // NOLINTBEGIN(bugprone-easily-swappable-parameters)
  MarketData(const char _type, const common::OrderId _order_id,
             common::TickerId& _ticker_id, const char _side,
             const common::Price _price,
             const common::Qty _qty)
      : type(common::charToMarketUpdateType(_type)),  //279
        order_id(_order_id),
        ticker_id(std::move(_ticker_id)),
        side(common::charToSide(_side)),  //269
        price(_price),
        qty(_qty) {}

  MarketData(const common::MarketUpdateType _type,
             const common::OrderId _order_id, common::TickerId& _ticker_id,
             const common::Side _side, const common::Price _price,
             const common::Qty _qty)
      : type(_type),
        order_id(_order_id),
        ticker_id(std::move(_ticker_id)),
        side(_side),
        price(_price),
        qty(_qty) {}

  MarketData(const common::MarketUpdateType type,
             const common::OrderId order_id, common::TickerId& ticker_id,
             const char side, const common::Price price, const common::Qty qty)
      : type(type),
        order_id(order_id),
        ticker_id(std::move(ticker_id)),
        side(common::charToSide(side)),
        price(price),
        qty(qty) {}
  // NOLINTEND(bugprone-easily-swappable-parameters)

  [[nodiscard]] auto toString() const noexcept {
    std::stringstream stream;
    stream << "MarketUpdate" << " ["
           << " type:" << common::marketUpdateTypeToString(type)
           << " ticker:" << ticker_id
           << " oid:" << common::orderIdToString(order_id)
           << " side:" << common::sideToString(side)
           << " qty:" << common::qtyToString(qty)
           << " price:" << common::priceToString(price) << "]";
    return stream.str();
  }
};

struct MarketUpdateData {
  std::vector<MarketData*> data;
};
#endif  //MARKET_DATA_H