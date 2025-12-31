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

#ifndef ORDERBOOK_HPP
#define ORDERBOOK_HPP

#include <array>
#include <cassert>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include "common/ini_config.hpp"
#include "common/logger.h"
#include "common/memory_pool.hpp"
#include "common/types.h"
#include "market_data.h"

struct MarketData;

namespace trading {
template <typename Strategy>
class TradeEngine;

struct BBO {
  common::PriceType bid_price = common::PriceType::from_raw(0);
  common::PriceType ask_price = common::PriceType::from_raw(0);
  common::QtyType bid_qty = common::QtyType::from_raw(0);
  common::QtyType ask_qty = common::QtyType::from_raw(0);

  [[nodiscard]] auto toString() const {
    std::ostringstream stream;
    stream << "BBO{" << common::toString(bid_qty) << "@"
           << common::toString(bid_price) << "X" << common::toString(ask_qty)
           << "@" << common::toString(ask_price) << "}";

    return stream.str();
  }
};

constexpr int kDefaultMinPriceInt = 100'000;
constexpr int kDefaultMaxPriceInt = 30'000'000;
constexpr int kDefaultTickMultiplierInt = 100;

constexpr int kBucketSize = 4096;
constexpr int kBitsPerWord = 64;
constexpr int kWordShift = 6;
constexpr int kWordMask = kBitsPerWord - 1;
constexpr int kBucketBitmapWords =
    (kBucketSize + kBitsPerWord - 1) / kBitsPerWord;

struct OrderBookConfig {
  int min_price_int;
  int max_price_int;
  int tick_multiplier_int;
  int num_levels;
  int bucket_count;
  int summary_words;

  static OrderBookConfig from_ini() {
    const int min_price =
        INI_CONFIG.get_int("orderbook", "min_price_int", kDefaultMinPriceInt);
    const int max_price =
        INI_CONFIG.get_int("orderbook", "max_price_int", kDefaultMaxPriceInt);
    const int tick_mult = INI_CONFIG.get_int("orderbook",
        "tick_multiplier_int",
        kDefaultTickMultiplierInt);

    const int num_levels = max_price - min_price + 1;
    const int bucket_count = (num_levels + kBucketSize - 1) / kBucketSize;
    const int summary_words = (bucket_count + kBitsPerWord - 1) / kBitsPerWord;

    return OrderBookConfig{
        .min_price_int = min_price,
        .max_price_int = max_price,
        .tick_multiplier_int = tick_mult,
        .num_levels = num_levels,
        .bucket_count = bucket_count,
        .summary_words = summary_words,
    };
  }

  [[nodiscard]] int price_to_index(common::PriceType price) const noexcept {
    // kPriceScale == tick_multiplier_int, so price.raw_value is the tick index
    return static_cast<int>(price.value) - min_price_int;
  }

  [[nodiscard]] common::PriceType index_to_price(int index) const noexcept {
    return common::PriceType::from_raw(
        static_cast<int64_t>(min_price_int) + static_cast<int64_t>(index));
  }
};

struct MarketOrder {
  common::QtyType qty = common::QtyType::from_raw(0);
  bool active = false;
  MarketOrder() noexcept = default;

  explicit MarketOrder(common::QtyType qty_, bool active_ = false) noexcept
      : qty(qty_), active(active_) {}

  [[nodiscard]] auto toString() const -> std::string {
    std::ostringstream stream;
    stream << "[MarketOrder]" << "[" << "qty:" << qty.to_double() << " "
           << "active:" << active << " ";
    return stream.str();
  }

  [[nodiscard]] bool is_positive() const noexcept { return qty.value > 0; }
  [[nodiscard]] double qty_as_double() const noexcept {
    return qty.to_double();
  }
  [[nodiscard]] int64_t qty_raw() const noexcept { return qty.value; }
};

struct Bucket {
  std::array<MarketOrder, kBucketSize> orders{};
  std::array<uint64_t, kBucketBitmapWords> bitmap{};

  [[nodiscard]] bool empty() const noexcept {
    return std::ranges::all_of(bitmap, [](auto word) { return word == 0; });
  }
};

// Legacy global functions - deprecated, use OrderBookConfig methods instead
// Kept for backward compatibility with existing code
inline int priceToIndex(common::PriceType price_int,
    const OrderBookConfig& cfg) noexcept {
  return cfg.price_to_index(price_int);
}

inline common::PriceType indexToPrice(int index,
    const OrderBookConfig& cfg) noexcept {
  return cfg.index_to_price(index);
}

struct LevelView {
  int idx;
  int64_t qty_raw;
  int64_t price_raw;

  [[nodiscard]] double qty() const noexcept {
    return static_cast<double>(qty_raw) / common::FixedPointConfig::kQtyScale;
  }

  [[nodiscard]] double price() const noexcept {
    return static_cast<double>(price_raw) /
           common::FixedPointConfig::kPriceScale;
  }
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

// NOLINTBEGIN
template <typename T>
inline bool push_if_active(const Bucket* bucket, int bidx, int off,
    std::span<T> qty_out, std::span<int> idx_out, int& filled, int want) {
  const auto& market_order = bucket->orders[off];
  if (market_order.active && market_order.is_positive()) {
    if constexpr (std::is_same_v<T, int64_t>) {
      qty_out[filled] = market_order.qty_raw();
    } else {
      qty_out[filled] = market_order.qty_as_double();
    }
    if (!idx_out.empty())
      idx_out[filled] = bidx * kBucketSize + off;
    ++filled;
  }
  return filled >= want;
}

template <bool MsbFirst>
bool scan_word(uint64_t word, int base_off,
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
bool scan_word(uint64_t word, int base_word_idx, const auto& on_off) {
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
uint64_t first_mask(int start_off) {
  if constexpr (S == common::Side::kBuy) {
    const int remain = (start_off & kWordMask);
    return (remain == 0) ? 0ULL : ((1ULL << remain) - 1);
  } else {
    const int remain = (start_off & kWordMask);
    return (remain == kWordMask) ? 0ULL : ~((1ULL << (remain + 1)) - 1);
  }
}

template <common::Side S, typename T>
bool consume_first_word(const Bucket* bucket, int bidx, int start_off,
    std::span<T> qty_out, std::span<int> idx_out, int& filled, int want) {
  const int word_idx = start_off >> kWordShift;
  const uint64_t mask = first_mask<S>(start_off);
  const uint64_t word = bucket->bitmap[word_idx] & mask;

  auto on_off = [&](int off) {
    return push_if_active(bucket, bidx, off, qty_out, idx_out, filled, want);
  };

  if constexpr (S == common::Side::kBuy) {
    return scan_word<true>(word, word_idx, on_off);
  } else {
    return scan_word<false>(word, word_idx, on_off);
  }
}

template <common::Side S, typename T>
bool consume_following_words(const Bucket* bucket, int bidx, int start_off,
    std::span<T> qty_out, std::span<int> idx_out, int& filled, int want) {
  const int word_idx = start_off >> kWordShift;

  auto on_off = [&](int off) {
    return push_if_active(bucket, bidx, off, qty_out, idx_out, filled, want);
  };

  if constexpr (S == common::Side::kBuy) {
    for (int idx = word_idx - 1; idx >= 0 && filled < want; --idx) {
      if (scan_word<true>(bucket->bitmap[idx], idx, on_off))
        return true;
    }
  } else {
    for (int idx = word_idx + 1; idx < kBucketBitmapWords && filled < want;
        ++idx) {
      if (scan_word<false>(bucket->bitmap[idx], idx, on_off))
        return true;
    }
  }
  return filled >= want;
}

template <common::Side S, typename T>
bool consume_bucket_side(const Bucket* bucket, int bidx, int start_off,
    std::span<T> qty_out, std::span<int> idx_out, int& filled, int want) {
  if (!bucket)
    return false;

  if (consume_first_word<S,
          T>(bucket, bidx, start_off, qty_out, idx_out, filled, want)) {
    return true;
  }
  return consume_following_words<S, T>(bucket,
      bidx,
      start_off,
      qty_out,
      idx_out,
      filled,
      want);
}

constexpr uint64_t mask_before(int bit) {
  return (bit == 0) ? 0ULL : ((1ULL << bit) - 1);
}

constexpr uint64_t mask_after_inclusive(int bit) {
  return ~((1ULL << (bit + 1)) - 1);
}

template <bool IsBid>
int scan_word_one(uint64_t word, int swi) {
  if (!word)
    return -1;
  const int bit =
      IsBid ? (kWordMask - __builtin_clzll(word)) : __builtin_ctzll(word);
  return (swi << kWordShift) + bit;
}

template <bool IsBid>
int jump_next_bucket_impl(std::span<const uint64_t> summary, int start_bidx) {
  const int kSummaryWords = static_cast<int>(summary.size());
  const int swi = (start_bidx >> kWordShift);
  const int sbit = (start_bidx & kWordMask);

  const uint64_t masked =
      summary[swi] &
      (IsBid ? mask_before(sbit)
             : ((sbit == kWordMask) ? 0ULL : mask_after_inclusive(sbit)));

  if (int idx = scan_word_one<IsBid>(masked, swi); idx != -1)
    return idx;

  if constexpr (IsBid) {
    for (int iter = swi - 1; iter >= 0; --iter)
      if (int index = scan_word_one<IsBid>(summary[iter], iter); index != -1)
        return index;
  } else {
    for (int i = swi + 1; i < kSummaryWords; ++i)
      if (int index = scan_word_one<IsBid>(summary[i], i); index != -1)
        return index;
  }
  return -1;
}

bool push_level_if_positive(const Bucket* bucket, int bucket_idx, int local_off,
    const auto& indexToPrice, std::vector<LevelView>& out, int level) {
  const MarketOrder* market_order = level_ptr(bucket, local_off);
  if (!market_order->is_positive())
    return false;
  const int global_idx = bucket_idx * kBucketSize + local_off;
  out.push_back(LevelView{global_idx,
      market_order->qty.value,
      indexToPrice(global_idx).value});
  return static_cast<int>(out.size()) >= level;
}

template <bool IsBid>
void consume_levels_in_bucket(const Bucket* bucket, int bucket_idx, int off,
    const auto& indexToPrice, std::vector<LevelView>& out, int level) {
  if (!bucket || static_cast<int>(out.size()) >= level)
    return;

  auto on_off = [&](int loc_off) {
    return push_level_if_positive(bucket,
        bucket_idx,
        loc_off,
        indexToPrice,
        out,
        level);
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
    if (scan_word<true>(word, word_index, on_off))
      return;
  } else {
    if (scan_word<false>(word, word_index, on_off))
      return;
  }

  if constexpr (IsBid) {
    for (int workd_idx = word_index - 1;
        workd_idx >= 0 && static_cast<int>(out.size()) < level;
        --workd_idx) {
      if (scan_word<true>(bucket->bitmap[workd_idx], workd_idx, on_off))
        return;
    }
  } else {
    for (int word_idx = word_index + 1;
        word_idx < kBucketBitmapWords && static_cast<int>(out.size()) < level;
        ++word_idx) {
      if (scan_word<false>(bucket->bitmap[word_idx], word_idx, on_off))
        return;
    }
  }
}

// NOLINTEND

template <typename Strategy>
class MarketOrderBook final {
  static constexpr int kBucketPoolSize = 1024 * 8;

 public:
  MarketOrderBook(const common::TickerId& ticker_id,
      const common::Logger::Producer& logger)
      : ticker_id_(std::move(ticker_id)),
        logger_(logger),
        config_(OrderBookConfig::from_ini()),
        bidBuckets_(config_.bucket_count, nullptr),
        askBuckets_(config_.bucket_count, nullptr),
        bidSummary_(config_.summary_words, 0),
        askSummary_(config_.summary_words, 0),
        bid_bucket_pool_(
            std::make_unique<common::MemoryPool<Bucket>>(kBucketPoolSize)),
        ask_bucket_pool_(
            std::make_unique<common::MemoryPool<Bucket>>(kBucketPoolSize)) {
    logger_.info(
        "[Constructor] MarketOrderBook Created - min_price: {}, max_price: {}, "
        "tick_mult: {}, bucket_count: {}, summary_words: {}",
        config_.min_price_int,
        config_.max_price_int,
        config_.tick_multiplier_int,
        config_.bucket_count,
        config_.summary_words);
  }

  ~MarketOrderBook() {
    logger_.info("[Destructor] MarketOrderBook Destroy");
    trade_engine_ = nullptr;
  }

  auto on_market_data_updated(const MarketData* market_update) noexcept
      -> void {

    const int64_t max_price_raw = config_.max_price_int;
    const int64_t min_price_raw = config_.min_price_int;

    if (market_update->price.value > max_price_raw ||
        market_update->price.value < min_price_raw) {
      logger_.error("common::Price[{}] is invalid (range: {} ~ {})",
          market_update->price.to_double(),
          static_cast<double>(min_price_raw) /
              common::FixedPointConfig::kPriceScale,
          static_cast<double>(max_price_raw) /
              common::FixedPointConfig::kPriceScale);
      return;
    }

    const int idx = config_.price_to_index(market_update->price);
    const auto qty = market_update->qty;

    switch (market_update->type) {
      case common::MarketUpdateType::kInvalid:
        logger_.error("error in market update data");
        break;
      case common::MarketUpdateType::kClear: {
        for (int i = 0; i < config_.bucket_count; ++i) {
          if (bidBuckets_[i]) {
            bid_bucket_pool_->deallocate(bidBuckets_[i]);
            bidBuckets_[i] = nullptr;
          }
          if (askBuckets_[i]) {
            ask_bucket_pool_->deallocate(askBuckets_[i]);
            askBuckets_[i] = nullptr;
          }
        }
        std::fill(bidSummary_.begin(), bidSummary_.end(), 0);
        std::fill(askSummary_.begin(), askSummary_.end(), 0);
        bbo_ = {.bid_price = common::PriceType::from_raw(0),
            .ask_price = common::PriceType::from_raw(0),
            .bid_qty = common::QtyType::from_raw(0),
            .ask_qty = common::QtyType::from_raw(0)};
        logger_.info("Cleared all market data.");
        return;
      }
      case common::MarketUpdateType::kAdd:
      case common::MarketUpdateType::kModify: {
        add_order(market_update, idx, qty);
        break;
      }
      case common::MarketUpdateType::kCancel: {
        delete_order(market_update, idx);
        break;
      }
      case common::MarketUpdateType::kTrade: {
        if (LIKELY(trade_engine_)) {
          trade_engine_->on_trade_updated(market_update, this);
        }

        trade_order(market_update, idx);
        return;
      }
      case common::MarketUpdateType::kBookTicker: {
        if (LIKELY(trade_engine_)) {
          trade_engine_->on_book_ticker_updated(market_update);
        }
        return;
      }
    }

    logger_.trace("[Updated] {} {}",
        market_update->toString(),
        bbo_.toString());

    trade_engine_->on_orderbook_updated(market_update->ticker_id,
        market_update->price,
        market_update->side,
        this);
  }

  auto set_trade_engine(TradeEngine<Strategy>* trade_engine) {
    trade_engine_ = trade_engine;
  }

  [[nodiscard]] const BBO* get_bbo() const noexcept { return &bbo_; }

  [[nodiscard]] std::string print_active_levels(bool is_bid) const {
    std::ostringstream stream;
    const auto& buckets = is_bid ? bidBuckets_ : askBuckets_;

    for (int bucket_idx = 0; bucket_idx < config_.bucket_count; ++bucket_idx) {
      const Bucket* bucket = buckets[bucket_idx];
      if (!bucket)
        continue;

      for (int off = 0; off < kBucketSize; ++off) {
        const MarketOrder& order = bucket->orders[off];
        if (order.active && order.is_positive()) {
          const int global_idx = bucket_idx * kBucketSize + off;
          const auto price = config_.index_to_price(global_idx);
          stream << (is_bid ? "[BID]" : "[ASK]") << " idx:" << global_idx
                 << " price:" << common::toString(price)
                 << " qty:" << common::toString(order.qty) << "\n";
        }
      }
    }

    return stream.str();
  }

  [[nodiscard]] int next_active_idx(const bool is_bid,
      const int start_idx) const noexcept {
    return is_bid ? next_active_bid(start_idx) : next_active_ask(start_idx);
  }

  [[nodiscard]] int next_active_bid(int start_idx) const noexcept {
    const auto& summary_bitmap = bidSummary_;
    const auto& buckets = bidBuckets_;

    const int bucket_index = start_idx / kBucketSize;
    const int level_offset = start_idx & (kBucketSize - 1);

    if (const Bucket* bucket = buckets[bucket_index]) {
      const int word_index = level_offset >> kWordShift;
      const int bit_offset = level_offset & kWordMask;

      const uint64_t mask = (bit_offset == 0 ? 0ULL : (1ULL << bit_offset) - 1);
      if (const uint64_t word = bucket->bitmap[word_index] & mask) {
        const int bit_index = kWordMask - __builtin_clzll(word);
        return bucket_index * kBucketSize +
               (word_index * kBitsPerWord + bit_index);
      }

      for (int wi = word_index - 1; wi >= 0; --wi) {
        if (const uint64_t word = bucket->bitmap[wi]) {
          const int bit_index = kWordMask - __builtin_clzll(word);
          return bucket_index * kBucketSize + (wi * kBitsPerWord + bit_index);
        }
      }
    }

    const int summary_word_index = bucket_index >> kWordShift;
    const int summary_bit_offset = bucket_index & kWordMask;
    const uint64_t sb_word =
        summary_bitmap[summary_word_index] &
        (summary_bit_offset == 0 ? 0ULL : ((1ULL << summary_bit_offset) - 1));
    if (sb_word) {
      const int bit = kWordMask - __builtin_clzll(sb_word);
      const int next_bucket_index = (summary_word_index << kWordShift) + bit;
      const int off_in_bucket =
          find_in_bucket(buckets[next_bucket_index], /*highest=*/true);
      return next_bucket_index * kBucketSize + off_in_bucket;
    }

    for (int swi = summary_word_index - 1; swi >= 0; --swi) {
      if (const uint64_t summary_word = summary_bitmap[swi]) {
        const int bit = kWordMask - __builtin_clzll(summary_word);
        const int next_bucket_index = (swi << kWordShift) + bit;
        const int off_in_bucket =
            find_in_bucket(buckets[next_bucket_index], /*highest=*/true);
        return next_bucket_index * kBucketSize + off_in_bucket;
      }
    }

    return -1;
  }

  [[nodiscard]] int next_active_ask(int start_idx) const noexcept {
    const auto& summary_bitmap = askSummary_;
    const auto& buckets = askBuckets_;

    const int bucket_index = start_idx / kBucketSize;
    const int level_offset = start_idx & (kBucketSize - 1);

    if (const Bucket* bucket = buckets[bucket_index]) {
      const int word_index = level_offset >> kWordShift;
      const int bit_offset = level_offset & kWordMask;
      const uint64_t mask =
          (bit_offset == kWordMask ? 0ULL : ~((1ULL << (bit_offset + 1)) - 1));
      if (const uint64_t word = bucket->bitmap[word_index] & mask) {
        const int bit_index = __builtin_ctzll(word);
        return bucket_index * kBucketSize +
               (word_index * kBitsPerWord + bit_index);
      }

      for (int iter = word_index + 1; iter < kBucketBitmapWords; ++iter) {
        if (const uint64_t word = bucket->bitmap[iter]) {
          const int bit_index = __builtin_ctzll(word);
          return bucket_index * kBucketSize + (iter * kBitsPerWord + bit_index);
        }
      }
    }

    const int summary_word_index = bucket_index >> kWordShift;
    const int summary_bit_offset = bucket_index & kWordMask;
    const uint64_t sb_word =
        summary_bitmap[summary_word_index] &
        (summary_bit_offset == kWordMask
                ? 0ULL
                : ~((1ULL << (summary_bit_offset + 1)) - 1));
    if (sb_word) {
      const int bit = __builtin_ctzll(sb_word);
      const int next_bucket_index = (summary_word_index << kWordShift) + bit;
      const int off_in_bucket =
          find_in_bucket(buckets[next_bucket_index], /*highest=*/false);
      return next_bucket_index * kBucketSize + off_in_bucket;
    }

    for (int iter = summary_word_index + 1; iter < config_.summary_words;
        ++iter) {
      if (const uint64_t summary_word = summary_bitmap[iter]) {
        const int bit = __builtin_ctzll(summary_word);
        const int next_bucket_index = (iter << kWordShift) + bit;
        const int off_in_bucket =
            find_in_bucket(buckets[next_bucket_index], /*highest=*/false);
        return next_bucket_index * kBucketSize + off_in_bucket;
      }
    }

    return -1;
  }

  [[nodiscard]] std::vector<int> peek_levels(bool is_bid, int level) const {
    std::vector<int> output;
    int idx = is_bid ? best_bid_idx() : best_ask_idx();
    while (idx >= 0 && output.size() < static_cast<size_t>(level)) {
      idx = next_active_idx(is_bid, idx);
      if (idx >= 0)
        output.push_back(idx);
    }
    return output;
  }

  [[nodiscard]] static int find_in_bucket(const Bucket* bucket,
      bool highest) noexcept {
    if (highest) {
      for (int iter = kBucketBitmapWords - 1; iter >= 0; --iter) {
        if (const uint64_t word = bucket->bitmap[iter]) {
          const int bit = kWordMask - __builtin_clzll(word);
          return iter * kBitsPerWord + bit;
        }
      }
    } else {
      for (int iter = 0; iter < kBucketBitmapWords; ++iter) {
        if (const uint64_t word = bucket->bitmap[iter]) {
          const int bit = __builtin_ctzll(word);
          return iter * kBitsPerWord + bit;
        }
      }
    }
    return -1;
  }

  // NOLINTBEGIN(readability-function-cognitive-complexity)
  [[nodiscard]] int peek_levels_with_qty(bool is_bid, int level,
      std::vector<LevelView>& out) const noexcept {
    out.clear();
    out.reserve(level);
    if (level <= 0)
      return 0;

    const auto& summary = is_bid ? bidSummary_ : askSummary_;
    const auto& buckets = is_bid ? bidBuckets_ : askBuckets_;

    const int idx = is_bid ? best_bid_idx() : best_ask_idx();
    if (idx < 0)
      return 0;

    int bucket_idx = bucket_of(idx);
    int off = offset_of(idx);

    auto price_of = [this](int gidx) -> common::PriceType {
      return config_.index_to_price(gidx);
    };

    while (bucket_idx >= 0 && bucket_idx < config_.bucket_count &&
           static_cast<int>(out.size()) < level) {
      const Bucket* bucket = buckets[bucket_idx];

      if (!bucket) {
        bucket_idx = is_bid ? jump_next_bucket_impl<true>(summary, bucket_idx)
                            : jump_next_bucket_impl<false>(summary, bucket_idx);
        if (bucket_idx < 0)
          break;
        off = is_bid ? (kBucketSize - 1) : 0;
        continue;
      }

      if (is_bid) {
        consume_levels_in_bucket<true>(bucket,
            bucket_idx,
            off,
            price_of,
            out,
            level);
      } else {
        consume_levels_in_bucket<false>(bucket,
            bucket_idx,
            off,
            price_of,
            out,
            level);
      }
      if (static_cast<int>(out.size()) >= level)
        break;

      bucket_idx = is_bid ? jump_next_bucket_impl<true>(summary, bucket_idx)
                          : jump_next_bucket_impl<false>(summary, bucket_idx);
      if (bucket_idx < 0)
        break;
      off = is_bid ? (kBucketSize - 1) : 0;
    }

    return static_cast<int>(out.size());
  }

  template <typename T>
  [[nodiscard]] int peek_qty(bool is_bid, int level, std::span<T> qty_out,
      std::span<int> idx_out) const noexcept {
    const auto want = std::min<int>(level, static_cast<int>(qty_out.size()));
    if (want <= 0)
      return -1;

    const auto& summary = is_bid ? bidSummary_ : askSummary_;
    const auto& buckets = is_bid ? bidBuckets_ : askBuckets_;

    int filled = 0;

    auto consume_bucket = [&](int bidx, int start_off) {
      const Bucket* bucket = buckets[bidx];
      if (!bucket)
        return false;

      if (is_bid) {
        return consume_bucket_side<common::Side::kBuy, T>(bucket,
            bidx,
            start_off,
            qty_out,
            idx_out,
            filled,
            want);
      }
      return consume_bucket_side<common::Side::kSell, T>(bucket,
          bidx,
          start_off,
          qty_out,
          idx_out,
          filled,
          want);
    };

    auto jump_next_bucket = [&](int bidx) -> int {
      if (is_bid) {
        return jump_next_bucket_impl<true /*Bid*/>(summary, bidx);
      }
      return jump_next_bucket_impl<false /*Ask*/>(summary, bidx);
    };

    const int idx = is_bid ? best_bid_idx() : best_ask_idx();
    if (idx < 0)
      return -1;

    int bidx = idx / kBucketSize;
    int off = idx & (kBucketSize - 1);

    if (consume_bucket(bidx, off))
      return filled;

    // NOLINTNEXTLINE(bugprone-infinite-loop) - filled is modified via reference in consume_bucket lambda
    while (filled < want) {
      bidx = jump_next_bucket(bidx);
      if (bidx < 0)
        break;
      off = is_bid ? (kBucketSize - 1) : 0;
      if (consume_bucket(bidx, off))
        break;
    }

    return filled;
  }

  // NOLINTEND(readability-function-cognitive-complexity)

  static void on_trade_update(MarketData* /*market_data*/) {}

  [[nodiscard]] const OrderBookConfig& config() const noexcept {
    return config_;
  }

  MarketOrderBook() = delete;
  MarketOrderBook(const MarketOrderBook&) = delete;
  MarketOrderBook(const MarketOrderBook&&) = delete;
  MarketOrderBook& operator=(const MarketOrderBook&) = delete;
  MarketOrderBook& operator=(const MarketOrderBook&&) = delete;

 private:
  const common::TickerId ticker_id_;
  TradeEngine<Strategy>* trade_engine_ = nullptr;
  const common::Logger::Producer& logger_;
  const OrderBookConfig config_;

  std::vector<Bucket*> bidBuckets_;
  std::vector<Bucket*> askBuckets_;
  std::vector<uint64_t> bidSummary_;
  std::vector<uint64_t> askSummary_;

  BBO bbo_;

  std::unique_ptr<common::MemoryPool<Bucket>> bid_bucket_pool_;
  std::unique_ptr<common::MemoryPool<Bucket>> ask_bucket_pool_;

  void update_bid(int idx, common::QtyType qty) {
    const int bucket_idx = idx / kBucketSize;
    const int off = idx & (kBucketSize - 1);

    if (!bidBuckets_[bucket_idx]) {
      bidBuckets_[bucket_idx] = bid_bucket_pool_->allocate();
      std::ranges::fill(bidBuckets_[bucket_idx]->bitmap, 0);
      for (auto& order : bidBuckets_[bucket_idx]->orders) {
        order.active = false;
        order.qty = common::QtyType::from_raw(0);
      }
    }
    Bucket* const bucket = bidBuckets_[bucket_idx];

    auto& order = bucket->orders[off];
    order.qty = qty;
    order.active = (qty.value > 0);

    const int word = off >> kWordShift;
    const uint64_t mask = (1ULL << (off & kWordMask));

    if (order.active) {
      bucket->bitmap[word] |= mask;
      setSummary(true, bucket_idx);
    } else {
      bucket->bitmap[word] &= ~mask;
      if (bucket->empty()) {
        clear_summary(true, bucket_idx);
        bid_bucket_pool_->deallocate(bucket);
        bidBuckets_[bucket_idx] = nullptr;
      }
    }
  }

  void update_ask(const int idx, const common::QtyType qty) {
    const int bidx = idx / kBucketSize;
    const int off = idx & (kBucketSize - 1);

    if (!askBuckets_[bidx]) {
      askBuckets_[bidx] = ask_bucket_pool_->allocate();
      std::ranges::fill(askBuckets_[bidx]->bitmap, 0);
      for (auto& order : askBuckets_[bidx]->orders) {
        order.active = false;
        order.qty = common::QtyType::from_raw(0);
      }
    }
    Bucket* bucket = askBuckets_[bidx];

    auto& order = bucket->orders[off];
    order.qty = qty;
    order.active = (qty.value > 0);

    const int word = off >> kWordShift;
    const uint64_t mask = (1ULL << (off & kWordMask));

    if (order.active) {
      bucket->bitmap[word] |= mask;
      setSummary(false, bidx);
    } else {
      bucket->bitmap[word] &= ~mask;
      if (bucket->empty()) {
        clear_summary(false, bidx);
        ask_bucket_pool_->deallocate(bucket);
        askBuckets_[bidx] = nullptr;
      }
    }
  }

  [[nodiscard]] int best_bid_idx() const noexcept {
    for (int sw = config_.summary_words - 1; sw >= 0; --sw) {
      const uint64_t word = bidSummary_[sw];
      if (!word)
        continue;

      const int bit = 63 - __builtin_clzll(word);
      const int bidx = (sw << kWordShift) + bit;
      Bucket* bucket = bidBuckets_[bidx];
      assert(bucket);

      for (int lw = kBucketBitmapWords - 1; lw >= 0; --lw) {
        const uint64_t lword = bucket->bitmap[lw];
        if (!lword)
          continue;

        const int lbit = 63 - __builtin_clzll(lword);
        return bidx * kBucketSize + lw * kBitsPerWord + lbit;
      }
    }
    return -1;
  }

  [[nodiscard]] int best_ask_idx() const noexcept {
    for (int sw = 0; sw < config_.summary_words; ++sw) {
      const uint64_t word = askSummary_[sw];
      if (!word)
        continue;

      const int bit = __builtin_ctzll(word);
      const int bidx = (sw << kWordShift) + bit;
      Bucket* bucket = askBuckets_[bidx];
      assert(bucket);

      for (int lw = 0; lw < kBucketBitmapWords; ++lw) {
        const uint64_t lword = bucket->bitmap[lw];
        if (!lword)
          continue;

        const int lbit = __builtin_ctzll(lword);
        return bidx * kBucketSize + lw * kBitsPerWord + lbit;
      }
    }
    return -1;
  }

  [[nodiscard]] common::PriceType best_bid_price() const noexcept {
    const int idx = best_bid_idx();
    return (idx >= 0) ? config_.index_to_price(idx)
                      : common::PriceType::from_raw(0);
  }

  [[nodiscard]] common::PriceType best_ask_price() const noexcept {
    const int idx = best_ask_idx();
    return (idx >= 0) ? config_.index_to_price(idx)
                      : common::PriceType::from_raw(0);
  }

  [[nodiscard]] common::QtyType best_bid_qty() const noexcept {
    const int idx = best_bid_idx();
    if (idx < 0)
      return common::QtyType::from_raw(0);
    const int bidx = idx / kBucketSize;
    const int off = idx & (kBucketSize - 1);
    Bucket* bucket = bidBuckets_[bidx];
    return bucket ? bucket->orders[off].qty : common::QtyType::from_raw(0);
  }

  [[nodiscard]] common::QtyType best_ask_qty() const noexcept {
    const int idx = best_ask_idx();
    if (idx < 0)
      return common::QtyType::from_raw(0);
    const int bidx = idx / kBucketSize;
    const int off = idx & (kBucketSize - 1);
    Bucket* bucket = askBuckets_[bidx];
    return bucket ? bucket->orders[off].qty : common::QtyType::from_raw(0);
  }

  void trade_order(const MarketData* market_update, const int idx) {
    const int bidx = idx / kBucketSize;
    const int off = idx & (kBucketSize - 1);

    if (market_update->side == common::Side::kBuy) {
      Bucket* bucket = bidBuckets_[bidx];
      if (bucket && bucket->orders[off].active) {
        bucket->orders[off].qty -= market_update->qty;
        if (bucket->orders[off].qty.value <= 0) {
          update_bid(idx, common::QtyType::from_raw(0));
        }
        bbo_.bid_price = best_bid_price();
        bbo_.bid_qty = best_bid_qty();
      }
    } else {
      Bucket* bucket = askBuckets_[bidx];
      if (bucket && bucket->orders[off].active) {
        bucket->orders[off].qty -= market_update->qty;
        if (bucket->orders[off].qty.value <= 0) {
          update_ask(idx, common::QtyType::from_raw(0));
        }
        bbo_.ask_price = best_ask_price();
        bbo_.ask_qty = best_ask_qty();
      }
    }
  }

  void delete_order(const MarketData* market_update, const int idx) {
    if (market_update->side == common::Side::kBuy) {
      update_bid(idx, common::QtyType::from_raw(0));
      bbo_.bid_price = best_bid_price();
      bbo_.bid_qty = best_bid_qty();
    } else {
      update_ask(idx, common::QtyType::from_raw(0));
      bbo_.ask_price = best_ask_price();
      bbo_.ask_qty = best_ask_qty();
    }
  }

  void add_order(const MarketData* market_update, const int idx,
      const common::QtyType qty) {
    if (market_update->side == common::Side::kBuy) {
      update_bid(idx, qty);
      bbo_.bid_price = best_bid_price();
      bbo_.bid_qty = best_bid_qty();
    } else {
      update_ask(idx, qty);
      bbo_.ask_price = best_ask_price();
      bbo_.ask_qty = best_ask_qty();
    }
  }

  void setSummary(bool is_bid, int bid_x) noexcept {
    auto& summary = is_bid ? bidSummary_ : askSummary_;
    summary[bid_x >> kWordShift] |= (1ULL << (bid_x & kWordMask));
  }

  void clear_summary(bool is_bid, int bid_x) noexcept {
    auto& summary = is_bid ? bidSummary_ : askSummary_;
    summary[bid_x >> kWordShift] &= ~(1ULL << (bid_x & kWordMask));
  }
};

template <typename Strategy>
using MarketOrderBookHashMap =
    std::map<std::string, std::unique_ptr<MarketOrderBook<Strategy>>>;
}  // namespace trading

#endif  // ORDERBOOK_HPP
