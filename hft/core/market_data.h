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

#include <common/types.h>
#include <common/ini_config.hpp>

constexpr int kNoRelatedSym = 146;

namespace core {

struct PriceLevel {
  double price{0.0};
  double qty{0.0};

  PriceLevel() = default;
  PriceLevel(double price, double qty) : price(price), qty(qty) {}
};

struct MarketDataMessage {
  enum class Type : std::uint8_t { kSnapshot, kIncremental, kTrade, kDepthUpdate };

  Type type{Type::kSnapshot};
  std::string symbol;
  uint64_t timestamp{0};

  std::vector<PriceLevel> bids;
  std::vector<PriceLevel> asks;

  struct TradeInfo {
    double price{0.0};
    double qty{0.0};
    common::Side side{common::Side::kInvalid};
    uint64_t trade_id{0};
  };
  std::optional<TradeInfo> trade;
};

}  // namespace core

struct MarketData {
  common::MarketUpdateType type = common::MarketUpdateType::kInvalid;
  common::OrderId order_id = common::OrderId{common::kOrderIdInvalid};
  common::TickerId ticker_id = common::kTickerIdInvalid;
  common::Side side = common::Side::kInvalid;
  common::Price price = common::Price{common::kPriceInvalid};
  common::Qty qty = common::Qty{common::kQtyInvalid};

  MarketData() noexcept = default;

  MarketData(const char _type, const common::OrderId _order_id,
      common::TickerId _ticker_id, const char _side, const common::Price _price,
      const common::Qty _qty) noexcept
      : type(common::charToMarketUpdateType(_type)),  //279
        order_id(_order_id),
        ticker_id(std::move(_ticker_id)),
        side(common::charToSide(_side)),  //269
        price(_price),
        qty(_qty) {}

  MarketData(const common::MarketUpdateType _type,
      const common::OrderId _order_id, common::TickerId _ticker_id,
      const common::Side _side, const common::Price _price,
      const common::Qty _qty) noexcept
      : type(_type),
        order_id(_order_id),
        ticker_id(std::move(_ticker_id)),
        side(_side),
        price(_price),
        qty(_qty) {}

  MarketData(const common::MarketUpdateType type,
      const common::OrderId order_id, common::TickerId ticker_id,
      const char side, const common::Price price,
      const common::Qty qty) noexcept
      : type(type),
        order_id(order_id),
        ticker_id(std::move(ticker_id)),
        side(common::charToSide(side)),
        price(price),
        qty(qty) {}

  [[nodiscard]] auto toString() const {
    std::ostringstream stream;
    stream << "MarketUpdate" << " [" << " type:" << common::toString(type)
           << " ticker:" << ticker_id << " oid:" << common::toString(order_id)
           << " side:" << common::toString(side)
           << " qty:" << common::toString(qty)
           << " price:" << common::toString(price) << "]";
    return stream.str();
  }
};

enum MarketDataType : uint8_t {
  kTrade = 0,
  kMarket,
  kNone,
};

struct MarketUpdateData {
  uint64_t start_idx = 0ULL;
  uint64_t end_idx = 0ULL;
  MarketDataType type;
  std::vector<MarketData*> data;
  explicit MarketUpdateData() = default;
  MarketUpdateData(MarketDataType _type, const std::vector<MarketData*>& _data)
      : type(_type), data(_data) {}

  MarketUpdateData(MarketDataType _type,
      std::vector<MarketData*>&& _data) noexcept
      : type(_type), data(std::move(_data)) {}

  MarketUpdateData(uint64_t _start_idx, uint64_t _end_idx, MarketDataType _type,
      const std::vector<MarketData*>& _data)
      : start_idx(_start_idx), end_idx(_end_idx), type(_type), data(_data) {}

  MarketUpdateData(uint64_t _start_idx, uint64_t _end_idx, MarketDataType _type,
      std::vector<MarketData*>&& _data) noexcept
      : start_idx(_start_idx),
        end_idx(_end_idx),
        type(_type),
        data(std::move(_data)) {}
};

struct InstrumentInfo {
  InstrumentInfo() {
    qty_precision = INI_CONFIG.get_int("meta", "qty_precision");
    price_precision = INI_CONFIG.get_int("meta", "price_precision");
  }
  std::string instrument_req_id;  // 320
  int no_related_sym = 0;         // 146
  int qty_precision;
  int price_precision;

  struct RelatedSymT {
    std::string symbol;                     // 55
    std::string currency;                   // 15
    double min_trade_vol = 0.0;             // 562
    double max_trade_vol = 0.0;             // 1140
    double min_qty_increment = 0.0;         // 25039
    double market_min_trade_vol = 0.0;      // 25040
    double market_max_trade_vol = 0.0;      // 25041
    double market_min_qty_increment = 0.0;  // 25042
    double min_price_increment = 0.0;       // 969

    [[nodiscard]] std::string toString(int qty_precision,
        int price_recision) const {
      std::ostringstream stream;
      stream.setf(std::ios::fixed, std::ios::floatfield);
      stream << "{symbol=" << symbol << ", currency=" << currency
             << ", min_trade_vol=" << std::setprecision(qty_precision)
             << min_trade_vol
             << ", max_trade_vol=" << std::setprecision(qty_precision)
             << max_trade_vol
             << ", min_qty_increment=" << std::setprecision(qty_precision)
             << min_qty_increment
             << ", market_min_trade_vol=" << std::setprecision(qty_precision)
             << market_min_trade_vol
             << ", market_max_trade_vol=" << std::setprecision(qty_precision)
             << market_max_trade_vol << ", market_min_qty_increment="
             << std::setprecision(qty_precision) << market_min_qty_increment
             << ", min_price_increment=" << std::setprecision(price_recision)
             << min_price_increment << "}";
      return stream.str();
    }
  };

  std::vector<RelatedSymT> symbols;

  [[nodiscard]] std::string toString() const {
    std::ostringstream stream;
    stream << "instrument_info{instrument_req_id=" << instrument_req_id
           << ", no_related_sym=" << no_related_sym << ", symbols=[";
    for (size_t idx = 0; idx < symbols.size(); ++idx) {
      if (idx)
        stream << ", ";
      stream << symbols[idx].toString(qty_precision, price_precision);
    }
    stream << "]}";
    return stream.str();
  }
};

struct MarketDataReject {
  std::string session_reject_reason;
  int rejected_message_type;
  std::string error_message;
  int error_code;
  [[nodiscard]] std::string toString() const {
    std::ostringstream stream;
    stream << "MarketDataReject{"
           << "session_reject_reason=" << session_reject_reason
           << ", rejected_message_type=" << rejected_message_type
           << ", error_code=" << error_code
           << ", error_message=" << std::quoted(error_message) << "}";
    return stream.str();
  }
};
#endif  //MARKET_DATA_H
