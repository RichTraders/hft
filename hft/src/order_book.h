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

#include "logger.h"
#include "market_data.h"
#include "memory_pool.hpp"
#include "types.h"

struct MarketData;

namespace trading {
class TradeEngine;

struct BBO {
  common::Price bid_price = common::Price{common::kPriceInvalid};
  common::Price ask_price = common::Price{common::kPriceInvalid};
  common::Qty bid_qty = common::Qty{common::kQtyInvalid};
  common::Qty ask_qty = common::Qty{common::kQtyInvalid};

  [[nodiscard]] auto toString() const {
    std::ostringstream stream;
    stream << "BBO{" << common::toString(bid_qty) << "@"
           << common::toString(bid_price) << "X" << common::toString(ask_price)
           << "@" << common::toString(ask_qty) << "}";

    return stream.str();
  }
};

constexpr int kMinPriceInt = 100'000;
constexpr int kMaxPriceInt = 30'000'000;
constexpr int kTickSizeInt = 1;
constexpr int kTickMultiplierInt = 100;  // price × 100
constexpr int kNumLevels = kMaxPriceInt - kMinPriceInt + 1;

constexpr int kBucketSize = 4096;
constexpr int kBucketCount = (kNumLevels + kBucketSize - 1) / kBucketSize;

constexpr int kBitsPerWord = 64;
constexpr int kWordShift = 6;
constexpr int kWordMask = kBitsPerWord - 1;
constexpr int kBucketBitmapWords =
    (kBucketSize + kBitsPerWord - 1) / kBitsPerWord;
constexpr int kSummaryWords = (kBucketCount + kBitsPerWord - 1) / kBitsPerWord;

struct MarketOrder {
  common::Qty qty = common::Qty{.0f};
  bool active = false;
  MarketOrder();
  MarketOrder(common::Qty qty_, bool active_) noexcept;
  [[nodiscard]] auto toString() const -> std::string;
};

struct Bucket {
  std::array<MarketOrder, kBucketSize> orders{};
  std::array<uint64_t, kBucketBitmapWords> bitmap{};

  [[nodiscard]] bool empty() const noexcept {
    return std::ranges::all_of(bitmap, [](auto word) { return word == 0; });
  }
};

inline int priceToIndex(common::Price price_int) noexcept {
  return static_cast<int>(price_int.value * kTickMultiplierInt) - kMinPriceInt;
}

inline common::Price indexToPrice(int index) noexcept {
  return common::Price{static_cast<double>(kMinPriceInt + index) /
                       kTickMultiplierInt};
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
  [[nodiscard]] std::string print_active_levels(bool is_bid) const;

  // is_bid=true => Price iterate direction High->Low, false => Price iterate direction Low->High
  [[nodiscard]] int next_active_idx(const bool is_bid,
                                    const int start_idx) const noexcept {
    return is_bid ? next_active_bid(start_idx) : next_active_ask(start_idx);
  }

  [[nodiscard]] int next_active_bid(int start_idx) const noexcept;
  [[nodiscard]] int next_active_ask(int start_idx) const noexcept;
  [[nodiscard]] std::vector<int> peek_levels(bool is_bid, int level) const;
  [[nodiscard]] static int find_in_bucket(const Bucket* bucket,
                                          bool highest) noexcept;

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

  std::array<Bucket*, kBucketCount> bidBuckets_{};
  std::array<Bucket*, kBucketCount> askBuckets_{};
  std::array<uint64_t, kSummaryWords> bidSummary_{};
  std::array<uint64_t, kSummaryWords> askSummary_{};

  BBO bbo_;

  std::unique_ptr<common::MemoryPool<Bucket>> bid_bucket_pool_;
  std::unique_ptr<common::MemoryPool<Bucket>> ask_bucket_pool_;

  void update_bid(int idx, common::Qty qty);
  void update_ask(int idx, common::Qty qty);

  [[nodiscard]] int best_bid_idx() const noexcept;
  [[nodiscard]] int best_ask_idx() const noexcept;

  [[nodiscard]] common::Price best_bid_price() const noexcept;
  [[nodiscard]] common::Price best_ask_price() const noexcept;

  [[nodiscard]] common::Qty best_bid_qty() const noexcept;
  [[nodiscard]] common::Qty best_ask_qty() const noexcept;

  void trade_order(const MarketData* market_update, int idx);
  void delete_order(const MarketData* market_update, int idx);
  void add_order(const MarketData* market_update, int idx, common::Qty qty);

  void setSummary(bool is_bid, int bid_x) noexcept {
    auto& summary = is_bid ? bidSummary_ : askSummary_;
    summary[bid_x >> kWordShift] |= (1ULL << (bid_x & kWordMask));
  }

  void clear_summary(bool is_bid, int bid_x) noexcept {
    auto& summary = is_bid ? bidSummary_ : askSummary_;
    summary[bid_x >> kWordShift] &= ~(1ULL << (bid_x & kWordMask));
  }
};

using MarketOrderBookHashMap =
    std::map<std::string, std::unique_ptr<MarketOrderBook>>;
}  // namespace trading

#endif  //ORDERBOOK_H