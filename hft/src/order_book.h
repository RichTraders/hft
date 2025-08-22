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
           << common::toString(bid_price) << "X" << common::toString(ask_qty)
           << "@" << common::toString(ask_price) << "}";

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

struct LevelView {
  int idx;
  double qty;
  double price;
};

inline int bucket_of(int idx) noexcept {
  return idx / kBucketSize;
}
inline int offset_of(int idx) noexcept {
  return idx & (kBucketSize - 1);
}

inline const MarketOrder* level_ptr(const Bucket* bucket, int off) noexcept {
  return &bucket->orders[off];
}

class MarketOrderBook final {
 public:
  MarketOrderBook(const common::TickerId& ticker_id, common::Logger* logger);

  ~MarketOrderBook();

  auto on_market_data_updated(const MarketData* market_update) noexcept -> void;

  auto set_trade_engine(TradeEngine* trade_engine) {
    trade_engine_ = trade_engine;
  }

  [[nodiscard]] const BBO* get_bbo() const noexcept { return &bbo_; }
  [[nodiscard]] std::string print_active_levels(bool is_bid) const;

  [[nodiscard]] int next_active_idx(const bool is_bid,
                                    const int start_idx) const noexcept {
    return is_bid ? next_active_bid(start_idx) : next_active_ask(start_idx);
  }

  [[nodiscard]] int next_active_bid(int start_idx) const noexcept;
  [[nodiscard]] int next_active_ask(int start_idx) const noexcept;
  [[nodiscard]] std::vector<int> peek_levels(bool is_bid, int level) const;
  [[nodiscard]] static int find_in_bucket(const Bucket* bucket,
                                          bool highest) noexcept;
  [[nodiscard]] int peek_levels_with_qty(
      bool is_bid, int level, std::vector<LevelView>& out) const noexcept;
  [[nodiscard]] int peek_qty(bool is_bid, int level, std::span<double> qty_out,
                             std::span<int> idx_out) const noexcept;

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

//NOLINTBEGIN
inline bool push_if_active(const Bucket* bucket, int bidx, int off,
                           int kBucketSize, std::span<double> qty_out,
                           std::span<int> idx_out, int& filled, int want) {
  const auto& market_order = bucket->orders[off];
  if (market_order.active && market_order.qty.value > 0.0) {
    qty_out[filled] = market_order.qty.value;
    if (!idx_out.empty())
      idx_out[filled] = bidx * kBucketSize + off;
    ++filled;
  }
  return filled >= want;
}

template <bool MsbFirst>
bool scan_word(uint64_t word, int base_off, int kWordShift, int kWordMask,
               const std::function<bool(int /*off*/)>& on_bit) {
  while (word) {
    int bit;
    if constexpr (MsbFirst) {
      bit = kWordMask - __builtin_clzll(word);
    } else {
      bit = __builtin_ctzll(word);
    }
    const int off = (base_off << kWordShift) + bit;
    if (on_bit(off))
      return true;  // filled >= want
    word &= (word - 1);
  }
  return false;
}

template <bool MsbFirst>
bool scan_word(uint64_t word, int base_word_idx, int kWordShift, int kWordMask,
               const auto& on_off) {
  while (word) {
    int bit =
        MsbFirst ? (kWordMask - __builtin_clzll(word)) : __builtin_ctzll(word);
    int off = (base_word_idx << kWordShift) + bit;
    if (on_off(off))
      return true;
    word &= (word - 1);
  }
  return false;
}

template <common::Side S>
uint64_t first_mask(int start_off, int kWordMask) {
  if constexpr (S == common::Side::kBuy) {
    const int remain = (start_off & kWordMask);
    return (remain == 0) ? 0ULL : ((1ULL << remain) - 1);
  } else {
    const int remain = (start_off & kWordMask);
    return (remain == kWordMask) ? 0ULL : ~((1ULL << (remain + 1)) - 1);
  }
}

template <common::Side S>
bool consume_first_word(const Bucket* bucket, int bidx, int start_off,
                        int kWordShift, int kWordMask,
                        std::span<double> qty_out, std::span<int> idx_out,
                        int& filled, int want) {
  const int word_idx = start_off >> kWordShift;
  const uint64_t mask = first_mask<S>(start_off, kWordMask);
  const uint64_t word = bucket->bitmap[word_idx] & mask;

  auto on_off = [&](int off) {
    return push_if_active(bucket, bidx, off, kBucketSize, qty_out, idx_out,
                          filled, want);
  };

  if constexpr (S == common::Side::kBuy) {
    return scan_word<true>(word, word_idx, kWordShift, kWordMask, on_off);
  } else {
    return scan_word<false>(word, word_idx, kWordShift, kWordMask, on_off);
  }
}

template <common::Side S>
bool consume_following_words(const Bucket* bucket, int bidx, int start_off,
                             int kWordShift, int kWordMask,
                             int kBucketBitmapWords, int kBucketSize,
                             std::span<double> qty_out, std::span<int> idx_out,
                             int& filled, int want) {
  const int word_idx = start_off >> kWordShift;

  auto on_off = [&](int off) {
    return push_if_active(bucket, bidx, off, kBucketSize, qty_out, idx_out,
                          filled, want);
  };

  if constexpr (S == common::Side::kBuy) {
    for (int idx = word_idx - 1; idx >= 0 && filled < want; --idx) {
      if (scan_word<true>(bucket->bitmap[idx], idx, kWordShift, kWordMask,
                          on_off))
        return true;
    }
  } else {
    for (int idx = word_idx + 1; idx < kBucketBitmapWords && filled < want;
         ++idx) {
      if (scan_word<false>(bucket->bitmap[idx], idx, kWordShift, kWordMask,
                           on_off))
        return true;
    }
  }
  return filled >= want;
}

template <common::Side S>
bool consume_bucket_side(const Bucket* bucket, int bidx, int start_off,
                         int kWordShift, int kWordMask, int kBucketSize,
                         int kBucketBitmapWords, std::span<double> qty_out,
                         std::span<int> idx_out, int& filled, int want) {
  if (!bucket)
    return false;

  if (consume_first_word<S>(bucket, bidx, start_off, kWordShift, kWordMask,
                            qty_out, idx_out, filled, want)) {
    return true;
  }
  return consume_following_words<S>(bucket, bidx, start_off, kWordShift,
                                    kWordMask, kBucketBitmapWords, kBucketSize,
                                    qty_out, idx_out, filled, want);
}

constexpr uint64_t mask_before(int bit) {
  return (bit == 0) ? 0ULL : ((1ULL << bit) - 1);
}
constexpr uint64_t mask_after_inclusive(int bit) {
  return ~((1ULL << (bit + 1)) - 1);
}

template <bool IsBid>
int scan_word_one(uint64_t word, int swi, int kWordShift, int kWordMask) {
  if (!word)
    return -1;
  const int bit =
      IsBid ? (kWordMask - __builtin_clzll(word)) : __builtin_ctzll(word);
  return (swi << kWordShift) + bit;
}

template <bool IsBid>
int jump_next_bucket_impl(std::span<const uint64_t> summary, int start_bidx,
                          int kWordShift, int kWordMask) {
  const int kSummaryWords = static_cast<int>(summary.size());
  const int swi = (start_bidx >> kWordShift);
  const int sbit = (start_bidx & kWordMask);

  // 1) 시작 word에서 start_bidx 기준으로 마스크
  const uint64_t masked =
      summary[swi] &
      (IsBid ? mask_before(sbit)  // Bid: 낮은 비트 쪽만
             : ((sbit == kWordMask)
                    ? 0ULL
                    : mask_after_inclusive(sbit)));  // Ask: 높은 비트 쪽만

  if (int idx = scan_word_one<IsBid>(masked, swi, kWordShift, kWordMask);
      idx != -1)
    return idx;

  if constexpr (IsBid) {
    for (int iter = swi - 1; iter >= 0; --iter)
      if (int index =
              scan_word_one<IsBid>(summary[iter], iter, kWordShift, kWordMask);
          index != -1)
        return index;
  } else {
    for (int i = swi + 1; i < kSummaryWords; ++i)
      if (int index =
              scan_word_one<IsBid>(summary[i], i, kWordShift, kWordMask);
          index != -1)
        return index;
  }
  return -1;
}
bool push_level_if_positive(const Bucket* bucket, int bucket_idx, int local_off,
                            int kBucketSize,
                            const auto& indexToPrice,  // member 호출자 전달용
                            std::vector<LevelView>& out, int level) {
  const MarketOrder* market_order = level_ptr(bucket, local_off);
  if (market_order->qty.value <= 0.0)
    return false;
  const int global_idx = bucket_idx * kBucketSize + local_off;
  out.push_back(LevelView{global_idx, market_order->qty.value,
                          indexToPrice(global_idx).value});
  return static_cast<int>(out.size()) >= level;
}

template <bool IsBid>
void consume_levels_in_bucket(const Bucket* bucket, int bucket_idx, int off,
                              int kWordShift, int kWordMask,
                              int kBucketBitmapWords, int kBucketSize,
                              const auto& indexToPrice,
                              std::vector<LevelView>& out, int level) {
  if (!bucket || static_cast<int>(out.size()) >= level)
    return;

  auto on_off = [&](int loc_off) {
    return push_level_if_positive(bucket, bucket_idx, loc_off, kBucketSize,
                                  indexToPrice, out, level);
  };

  const int word_index = off >> kWordShift;

  uint64_t first_mask;
  const int bit = off & kWordMask;
  if constexpr (IsBid) {
    if (bit == 0) {
      first_mask = 0ULL;
    } else {
      first_mask = mask_before(bit);
    }
  } else {
    if (bit == kWordMask) {
      first_mask = 0ULL;
    } else {
      first_mask = mask_after_inclusive(bit);
    }
  }

  const uint64_t word = bucket->bitmap[word_index] & first_mask;
  if constexpr (IsBid) {
    if (scan_word<true>(word, word_index, kWordShift, kWordMask, on_off))
      return;
  } else {
    if (scan_word<false>(word, word_index, kWordShift, kWordMask, on_off))
      return;
  }

  if constexpr (IsBid) {
    for (int workd_idx = word_index - 1;
         workd_idx >= 0 && static_cast<int>(out.size()) < level; --workd_idx) {
      if (scan_word<true>(bucket->bitmap[workd_idx], workd_idx, kWordShift,
                          kWordMask, on_off))
        return;
    }
  } else {
    for (int word_idx = word_index + 1;
         word_idx < kBucketBitmapWords && static_cast<int>(out.size()) < level;
         ++word_idx) {
      if (scan_word<false>(bucket->bitmap[word_idx], word_idx, kWordShift,
                           kWordMask, on_off))
        return;
    }
  }
}
//NOLINTEND
}  // namespace trading

#endif  //ORDERBOOK_H