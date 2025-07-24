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

#ifndef ORDERBOOK_H
#define ORDERBOOK_H

#include <bits/basic_string.h>

#include <cassert>

#include "logger.h"
#include "market_data.h"
#include "types.h"

struct MarketData;

namespace trading {
class TradeEngine;

struct BBO {
  common::Price bid_price = common::kPriceInvalid,
                ask_price = common::kPriceInvalid;
  common::Qty bid_qty = common::kQtyInvalid, ask_qty = common::kQtyInvalid;

  [[nodiscard]] auto toString() const {
    std::stringstream stream;
    stream << "BBO{" << common::qtyToString(bid_qty) << "@"
           << common::priceToString(bid_price) << "X"
           << common::priceToString(ask_price) << "@"
           << common::qtyToString(ask_qty) << "}";

    return stream.str();
  }
};

constexpr int kMinPriceInt = 2000000;
constexpr int kMaxPriceInt = 20000000;
constexpr int kTickSizeInt = 1;  // 0.01 = 1 tick
constexpr int kTickMultiplierInt = kTickSizeInt * 100;
constexpr int kNumLevels = kMaxPriceInt - kMinPriceInt + 1;

static constexpr int kBitsPerWord = 64;
static constexpr int kWordShift = 6;                // 2^6==64
static constexpr int kWordMask = kBitsPerWord - 1;  // 63
static constexpr int kBitmapWords = (kNumLevels + kWordMask) / kBitsPerWord;

struct MarketOrder {
  common::Price qty = .0f;
  bool active = false;
  MarketOrder();
  MarketOrder(common::Qty qty_, bool active_) noexcept;
  [[nodiscard]] auto toString() const -> std::string;
};

inline int priceToIndex(common::Price price_int) noexcept {
  return static_cast<int>(price_int * kTickMultiplierInt) - kMinPriceInt;
}

inline common::Price indexToPrice(int index) noexcept {
  return static_cast<common::Price>(kMinPriceInt + index) / kTickMultiplierInt;
}

inline void set_bit(std::array<uint64_t, kBitmapWords>& bitmap,
                    const int idx) noexcept {
  bitmap[idx >> kWordShift] |= (static_cast<uint64_t>(1) << (idx & kWordMask));
}

inline void clear_bit(std::array<uint64_t, kBitmapWords>& bitmap,
                      const int idx) noexcept {
  bitmap[idx >> kWordShift] &= ~(static_cast<uint64_t>(1) << (idx & kWordMask));
}

class MarketOrderBook final {
 public:
  MarketOrderBook(const common::TickerId& ticker_id, common::Logger* logger);

  ~MarketOrderBook();

  auto on_market_data_updated(const MarketData* market_update) noexcept -> void;

  auto set_trade_engine(TradeEngine* trade_engine) {
    trade_engine_ = trade_engine;
  }

  [[nodiscard]] auto get_bbo() noexcept -> const BBO*;
  static void on_trade_update(MarketData* market_data);

  MarketOrderBook() = delete;
  MarketOrderBook(const MarketOrderBook&) = delete;
  MarketOrderBook(const MarketOrderBook&&) = delete;
  MarketOrderBook& operator=(const MarketOrderBook&) = delete;
  MarketOrderBook& operator=(const MarketOrderBook&&) = delete;

 private:
  const common::TickerId ticker_id_;
  TradeEngine* trade_engine_ = nullptr;
  common::Logger* logger_ = nullptr;

  std::array<MarketOrder, kNumLevels> bids_{};
  std::array<MarketOrder, kNumLevels> asks_{};

  std::array<uint64_t, kBitmapWords> bidBitmap_{};
  std::array<uint64_t, kBitmapWords> askBitmap_{};

  BBO bbo_{.bid_price = common::kPriceInvalid,
           .ask_price = common::kQtyInvalid,
           .bid_qty = common::kPriceInvalid,
           .ask_qty = common::kQtyInvalid};

  void add_order(const MarketData* market_update);
  void modify_order(const MarketData* market_update);
  void delete_order(const MarketData* market_update);
  void trade_order(const MarketData* market_update);

  void activate_level(const bool isBid, const int idx) {
    assert(idx >= 0 && idx < kNumLevels);
    set_bit(isBid ? bidBitmap_ : askBitmap_, idx);
  }

  void clear_level(const bool isBid, const int idx) {
    assert(idx >= 0 && idx < kNumLevels);
    clear_bit(isBid ? bidBitmap_ : askBitmap_, idx);
  }

  [[nodiscard]] int best_bid_idx() const {
    // high word → low word 순서로 scan
    for (int words = kBitmapWords - 1; words >= 0; --words) {
      if (const uint64_t word = bidBitmap_[words]) {
        const int bit = kWordMask - __builtin_clzll(word);
        return (words * kBitsPerWord) + bit;
      }
    }
    return -1;
  }

  [[nodiscard]] int best_ask_idx() const {
    for (int words = 0; words < kBitmapWords; ++words) {
      const uint64_t word = askBitmap_[words];
      if (word) {
        const int bit = __builtin_ctzll(word);
        return (words * kBitsPerWord) + bit;
      }
    }
    return -1;
  }

  [[nodiscard]] float best_bid_price() const {
    const int idx = best_bid_idx();
    return (idx >= 0) ? indexToPrice(idx) : common::kPriceInvalid;
  }

  [[nodiscard]] float best_ask_price() const {
    const int idx = best_ask_idx();
    return (idx >= 0) ? indexToPrice(idx) : common::kPriceInvalid;
  }
};

using MarketOrderBookHashMap =
    std::map<std::string, std::unique_ptr<MarketOrderBook>>;
}  // namespace trading

#endif  //ORDERBOOK_H