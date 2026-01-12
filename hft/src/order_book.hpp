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
#include <bit>
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
#ifndef USE_FLAT_ORDERBOOK
#include "common/memory_pool.hpp"
#endif
#include "common/performance.h"
#include "common/types.h"
#include "market_data.h"

struct MarketData;

namespace trading {
template <typename Strategy>
class TradeEngine;

struct BBO {
  common::PriceType bid_price;
  common::PriceType ask_price;
  common::QtyType bid_qty;
  common::QtyType ask_qty;

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
constexpr int kMaxBucketPoolSize = 256;

constexpr int kBitsPerWord = 64;
constexpr int kWordShift = 6;
constexpr int kWordMask = kBitsPerWord - 1;

#ifndef USE_FLAT_ORDERBOOK
constexpr int kBucketSize = 4096;
constexpr int kBucketBitmapWords =
    (kBucketSize + kBitsPerWord - 1) / kBitsPerWord;
#endif

struct SearchHighest {
  static constexpr bool kIsHighest = true;

  [[nodiscard]] static int find_bit(uint64_t word) noexcept {
    return kWordMask - std::countl_zero(word);
  }

  [[nodiscard]] static uint64_t make_mask(int bit_offset) noexcept {
    return bit_offset == 0 ? 0ULL : (1ULL << bit_offset) - 1;
  }

  template <typename Func>
  static void iterate_words(int start, int end, Func&& func) noexcept {
    for (int i = start - 1; i >= end; --i) {
      if (func(i))
        return;
    }
  }

  template <typename Func>
  static void iterate_summary(int start, int /*end*/, Func&& func) noexcept {
    for (int i = start - 1; i >= 0; --i) {
      if (func(i))
        return;
    }
  }
};

struct SearchLowest {
  static constexpr bool kIsHighest = false;

  [[nodiscard]] static int find_bit(uint64_t word) noexcept {
    return std::countr_zero(word);
  }

  [[nodiscard]] static uint64_t make_mask(int bit_offset) noexcept {
    return bit_offset == kWordMask ? 0ULL : ~((1ULL << (bit_offset + 1)) - 1);
  }

  template <typename Func>
  static void iterate_words(int start, int end, Func&& func) noexcept {
    for (int i = start + 1; i < end; ++i) {
      if (func(i))
        return;
    }
  }

  template <typename Func>
  static void iterate_summary(int start, int end, Func&& func) noexcept {
    for (int i = start + 1; i < end; ++i) {
      if (func(i))
        return;
    }
  }
};

struct OrderBookConfig {
  int min_price_int;
  int max_price_int;
  int num_levels;
  int bitmap_words;      // Used by Flat OrderBook
  int bucket_count;      // Used by Bucket OrderBook
  int summary_words;     // Used by Bucket OrderBook
  int bucket_pool_size;  // Used by Bucket OrderBook

  static OrderBookConfig from_ini() {
    const int min_price =
        INI_CONFIG.get_int("orderbook", "min_price_int", kDefaultMinPriceInt);
    const int max_price =
        INI_CONFIG.get_int("orderbook", "max_price_int", kDefaultMaxPriceInt);

    const int num_levels = max_price - min_price + 1;
    const int bitmap_words = (num_levels + kBitsPerWord - 1) / kBitsPerWord;

#ifdef USE_FLAT_ORDERBOOK
    return OrderBookConfig{
        .min_price_int = min_price,
        .max_price_int = max_price,
        .num_levels = num_levels,
        .bitmap_words = bitmap_words,
        .bucket_count = 0,
        .summary_words = 0,
        .bucket_pool_size = 0,
    };
#else
    const int bucket_count = (num_levels + kBucketSize - 1) / kBucketSize;
    const int summary_words = (bucket_count + kBitsPerWord - 1) / kBitsPerWord;
    const int bucket_pool_size = std::min(bucket_count, kMaxBucketPoolSize);

    return OrderBookConfig{
        .min_price_int = min_price,
        .max_price_int = max_price,
        .num_levels = num_levels,
        .bitmap_words = bitmap_words,
        .bucket_count = bucket_count,
        .summary_words = summary_words,
        .bucket_pool_size = bucket_pool_size,
    };
#endif
  }

  [[nodiscard]] int price_to_index(common::PriceType price) const noexcept {
    return static_cast<int>(price.value) - min_price_int;
  }

  [[nodiscard]] common::PriceType index_to_price(int index) const noexcept {
    return common::PriceType::from_raw(
        static_cast<int64_t>(min_price_int) + static_cast<int64_t>(index));
  }
};

struct MarketOrder {
  common::QtyType qty = common::QtyType::from_raw(0);
  MarketOrder() noexcept = default;

  explicit MarketOrder(common::QtyType qty_) noexcept : qty(qty_) {}

  [[nodiscard]] auto toString() const -> std::string {
    std::ostringstream stream;
    stream << "[MarketOrder][qty:" << qty.to_double() << "]";
    return stream.str();
  }

  [[nodiscard]] bool is_active() const noexcept { return qty.value > 0; }
  [[nodiscard]] double qty_as_double() const noexcept {
    return qty.to_double();
  }
  [[nodiscard]] int64_t qty_raw() const noexcept { return qty.value; }
  void clear() noexcept { qty = common::QtyType::from_raw(0); }
};

#ifndef USE_FLAT_ORDERBOOK
struct Bucket {
  std::array<uint64_t, kBucketBitmapWords> bitmap{};
  std::array<MarketOrder, kBucketSize> orders{};

  [[nodiscard]] bool empty() const noexcept {
    return std::ranges::all_of(bitmap, [](auto word) { return word == 0; });
  }
};
#endif

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

#ifndef USE_FLAT_ORDERBOOK
// NOLINTBEGIN
template <typename T>
inline bool push_if_active(const Bucket* bucket, int bidx, int off,
    std::span<T> qty_out, std::span<int> idx_out, int& filled, int want) {
  const auto& market_order = bucket->orders[off];
  if (market_order.is_active()) {
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

template <bool MsbFirst, typename Fn>
bool scan_word(uint64_t word, int base_word_idx, Fn&& on_off) {
  while (word) {
    const int bit = MsbFirst ? (kWordMask - std::countl_zero(word))
                             : std::countr_zero(word);
    const int off = (base_word_idx << kWordShift) + bit;
    if (on_off(off))
      return true;
    if constexpr (MsbFirst) {
      word &= ~(1ULL << bit);
    } else {
      word &= (word - 1);
    }
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
      IsBid ? (kWordMask - std::countl_zero(word)) : std::countr_zero(word);
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
  const MarketOrder& market_order = bucket->orders[local_off];
  if (!market_order.is_active())
    return false;
  const int global_idx = bucket_idx * kBucketSize + local_off;
  out.push_back(LevelView{global_idx,
      market_order.qty.value,
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
#endif  // USE_FLAT_ORDERBOOK

#ifdef USE_FLAT_ORDERBOOK
// ============================================================================
// Flat Array based OrderBook (USE_FLAT_ORDERBOOK)
// ============================================================================
template <typename Strategy>
class MarketOrderBook final {
 public:
  MarketOrderBook(const common::TickerId& ticker_id,
      const common::Logger::Producer& logger)
      : ticker_id_(std::move(ticker_id)),
        logger_(logger),
        config_(OrderBookConfig::from_ini()),
        bid_orders_(config_.num_levels),
        ask_orders_(config_.num_levels),
        bid_bitmap_(config_.bitmap_words, 0),
        ask_bitmap_(config_.bitmap_words, 0),
        bid_summary_((config_.bitmap_words + kBitsPerWord - 1) / kBitsPerWord,
            0),
        ask_summary_((config_.bitmap_words + kBitsPerWord - 1) / kBitsPerWord,
            0) {
    LOG_INFO(logger_,
        "[Constructor] MarketOrderBook(Flat) Created - min_price: {}, "
        "max_price: {}, num_levels: {}, bitmap_words: {}",
        config_.min_price_int,
        config_.max_price_int,
        config_.num_levels,
        config_.bitmap_words);
  }

  ~MarketOrderBook() {
    LOG_INFO(logger_, "[Destructor] MarketOrderBook(Flat) Destroy");
    trade_engine_ = nullptr;
  }

  auto on_market_data_updated(
      const MarketData* market_update) noexcept -> void {
    const int64_t max_price_raw = config_.max_price_int;
    const int64_t min_price_raw = config_.min_price_int;

    if (market_update->price.value > max_price_raw ||
        market_update->price.value < min_price_raw) {
      LOG_ERROR(logger_,
          "common::Price[{}] is invalid (range: {} ~ {})",
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
        LOG_ERROR(logger_, "error in market update data");
        break;
      case common::MarketUpdateType::kClear: {
        for (auto& order : bid_orders_)
          order.clear();
        for (auto& order : ask_orders_)
          order.clear();
        std::fill(bid_bitmap_.begin(), bid_bitmap_.end(), 0);
        std::fill(ask_bitmap_.begin(), ask_bitmap_.end(), 0);
        std::fill(bid_summary_.begin(), bid_summary_.end(), 0);
        std::fill(ask_summary_.begin(), ask_summary_.end(), 0);
        bbo_ = {.bid_price = common::PriceType{},
            .ask_price = common::PriceType{},
            .bid_qty = common::QtyType{},
            .ask_qty = common::QtyType{}};
        LOG_INFO(logger_, "Cleared all market data.");
        return;
      }
      case common::MarketUpdateType::kAdd:
      case common::MarketUpdateType::kModify: {
        START_MEASURE(ORDERBOOK_APPLY);
        add_order(market_update, idx, qty);
        END_MEASURE(ORDERBOOK_APPLY, logger_);
        break;
      }
      case common::MarketUpdateType::kCancel: {
        START_MEASURE(ORDERBOOK_APPLY);
        delete_order(market_update, idx);
        END_MEASURE(ORDERBOOK_APPLY, logger_);
        break;
      }
      case common::MarketUpdateType::kTrade: {
        if (LIKELY(trade_engine_)) {
          trade_engine_->on_trade_updated(market_update, this);
        }
        return;
      }
      case common::MarketUpdateType::kBookTicker: {
        if (LIKELY(trade_engine_)) {
          trade_engine_->on_book_ticker_updated(market_update);
        }
        return;
      }
    }

    LOG_TRACE(logger_,
        "[Updated] {} {}",
        market_update->toString(),
        bbo_.toString());
    if (LIKELY(trade_engine_)) {
      trade_engine_->on_orderbook_updated(market_update->ticker_id,
          market_update->price,
          market_update->side,
          this);
    }
  }

  auto set_trade_engine(TradeEngine<Strategy>* trade_engine) {
    trade_engine_ = trade_engine;
  }

  [[nodiscard]] const BBO* get_bbo() const noexcept { return &bbo_; }

  [[nodiscard]] std::string print_active_levels(bool is_bid) const {
    std::ostringstream stream;
    const auto& orders = is_bid ? bid_orders_ : ask_orders_;
    const auto& bitmap = is_bid ? bid_bitmap_ : ask_bitmap_;

    for (int wi = 0; wi < config_.bitmap_words; ++wi) {
      uint64_t word = bitmap[wi];
      while (word) {
        const int bit = __builtin_ctzll(word);
        const int idx = (wi << kWordShift) + bit;
        if (idx < config_.num_levels && orders[idx].is_active()) {
          const auto price = config_.index_to_price(idx);
          stream << (is_bid ? "[BID]" : "[ASK]") << " idx:" << idx
                 << " price:" << common::toString(price)
                 << " qty:" << common::toString(orders[idx].qty) << "\n";
        }
        word &= (word - 1);
      }
    }
    return stream.str();
  }

  [[nodiscard]] int next_active_idx(const bool is_bid,
      const int start_idx) const noexcept {
    return is_bid ? next_active_bid(start_idx) : next_active_ask(start_idx);
  }

  [[nodiscard]] int next_active_bid(int start_idx) const noexcept {
    if (start_idx <= 0)
      return -1;

    const int word_idx = (start_idx - 1) >> kWordShift;
    if (word_idx >= config_.bitmap_words)
      return -1;

    const int bit_pos = (start_idx - 1) & kWordMask;

    const uint64_t mask = (1ULL << (bit_pos + 1)) - 1;
    if (const uint64_t word = bid_bitmap_[word_idx] & mask) {
      return (word_idx << kWordShift) + (kWordMask - __builtin_clzll(word));
    }

    for (int wi = word_idx - 1; wi >= 0; --wi) {
      if (const uint64_t word = bid_bitmap_[wi]) {
        return (wi << kWordShift) + (kWordMask - __builtin_clzll(word));
      }
    }
    return -1;
  }

  [[nodiscard]] int next_active_ask(int start_idx) const noexcept {
    if (start_idx >= config_.num_levels - 1)
      return -1;

    const int word_idx = (start_idx + 1) >> kWordShift;
    if (word_idx >= config_.bitmap_words)
      return -1;

    const int bit_pos = (start_idx + 1) & kWordMask;

    const uint64_t mask = ~((1ULL << bit_pos) - 1);
    if (const uint64_t word = ask_bitmap_[word_idx] & mask) {
      return (word_idx << kWordShift) + __builtin_ctzll(word);
    }

    for (int wi = word_idx + 1; wi < config_.bitmap_words; ++wi) {
      if (const uint64_t word = ask_bitmap_[wi]) {
        return (wi << kWordShift) + __builtin_ctzll(word);
      }
    }
    return -1;
  }

  [[nodiscard]] std::vector<int> peek_levels(bool is_bid, int level) const {
    std::vector<int> output;
    int idx = is_bid ? best_bid_idx() : best_ask_idx();
    if (idx >= 0) {
      output.push_back(idx);
    }
    while (idx >= 0 && output.size() < static_cast<size_t>(level)) {
      idx = next_active_idx(is_bid, idx);
      if (idx >= 0)
        output.push_back(idx);
    }
    return output;
  }

  [[nodiscard]] int peek_levels_with_qty(bool is_bid, int level,
      std::vector<LevelView>& out) const noexcept {
    out.clear();
    out.reserve(level);
    if (level <= 0)
      return 0;

    const auto& orders = is_bid ? bid_orders_ : ask_orders_;

    int idx = is_bid ? best_bid_idx() : best_ask_idx();
    if (idx < 0)
      return 0;

    if (orders[idx].is_active()) {
      out.push_back(LevelView{idx,
          orders[idx].qty.value,
          config_.index_to_price(idx).value});
    }

    while (static_cast<int>(out.size()) < level) {
      idx = next_active_idx(is_bid, idx);
      if (idx < 0)
        break;
      if (orders[idx].is_active()) {
        out.push_back(LevelView{idx,
            orders[idx].qty.value,
            config_.index_to_price(idx).value});
      }
    }
    return static_cast<int>(out.size());
  }

  template <typename T>
  [[nodiscard]] int peek_qty(bool is_bid, int level, std::span<T> qty_out,
      std::span<int> idx_out) const noexcept {
    const auto want = std::min<int>(level, static_cast<int>(qty_out.size()));
    if (want <= 0)
      return -1;

    const auto& orders = is_bid ? bid_orders_ : ask_orders_;
    int filled = 0;

    int idx = is_bid ? best_bid_idx() : best_ask_idx();
    if (idx < 0)
      return -1;

    if (orders[idx].is_active()) {
      if constexpr (std::is_same_v<T, int64_t>) {
        qty_out[filled] = orders[idx].qty_raw();
      } else {
        qty_out[filled] = orders[idx].qty_as_double();
      }
      if (!idx_out.empty())
        idx_out[filled] = idx;
      ++filled;
    }

    // NOLINTBEGIN(bugprone-infinite-loop)
    while (filled < want) {
      idx = next_active_idx(is_bid, idx);
      if (idx < 0)
        break;
      if (orders[idx].is_active()) {
        if constexpr (std::is_same_v<T, int64_t>) {
          qty_out[filled] = orders[idx].qty_raw();
        } else {
          qty_out[filled] = orders[idx].qty_as_double();
        }
        if (!idx_out.empty())
          idx_out[filled] = idx;
        ++filled;
      }
    }
    // NOLINTEND(bugprone-infinite-loop)
    return filled;
  }

  [[nodiscard]] const OrderBookConfig& config() const noexcept {
    return config_;
  }

  [[nodiscard]] int64_t get_qty_at_idx(bool is_bid, int idx) const noexcept {
    if (idx < 0 || idx >= config_.num_levels)
      return 0;
    return is_bid ? bid_orders_[idx].qty.value : ask_orders_[idx].qty.value;
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

  std::vector<MarketOrder> bid_orders_;
  std::vector<MarketOrder> ask_orders_;
  std::vector<uint64_t> bid_bitmap_;
  std::vector<uint64_t> ask_bitmap_;

  std::vector<uint64_t> bid_summary_;
  std::vector<uint64_t> ask_summary_;

  mutable int cached_best_bid_idx_ = -1;
  mutable int cached_best_ask_idx_ = -1;

  BBO bbo_;

  void update_bid(int idx, common::QtyType qty) {
    bid_orders_[idx].qty = qty;
    const int word = idx >> kWordShift;
    const uint64_t mask = 1ULL << (idx & kWordMask);

    if (qty.value > 0) {
      if (bid_bitmap_[word] == 0) {
        const int summary_idx = word >> kWordShift;
        bid_summary_[summary_idx] |= (1ULL << (word & kWordMask));
      }
      bid_bitmap_[word] |= mask;
    } else {
      bid_bitmap_[word] &= ~mask;
      if (bid_bitmap_[word] == 0) {
        const int summary_idx = word >> kWordShift;
        bid_summary_[summary_idx] &= ~(1ULL << (word & kWordMask));
      }
    }
  }

  void update_ask(int idx, common::QtyType qty) {
    ask_orders_[idx].qty = qty;
    const int word = idx >> kWordShift;
    const uint64_t mask = 1ULL << (idx & kWordMask);

    if (qty.value > 0) {
      if (ask_bitmap_[word] == 0) {
        const int summary_idx = word >> kWordShift;
        ask_summary_[summary_idx] |= (1ULL << (word & kWordMask));
      }
      ask_bitmap_[word] |= mask;
    } else {
      ask_bitmap_[word] &= ~mask;
      if (ask_bitmap_[word] == 0) {
        const int summary_idx = word >> kWordShift;
        ask_summary_[summary_idx] &= ~(1ULL << (word & kWordMask));
      }
    }
  }

  [[nodiscard]] int best_bid_idx() const noexcept {
    if (cached_best_bid_idx_ >= 0 &&
        bid_orders_[cached_best_bid_idx_].is_active()) {
      return cached_best_bid_idx_;
    }

    const int summary_words = static_cast<int>(bid_summary_.size());
    for (int sw = summary_words - 1; sw >= 0; --sw) {
      uint64_t s_word = bid_summary_[sw];
      if (!s_word)
        continue;

      int s_bit = kWordMask - __builtin_clzll(s_word);
      int word_idx = (sw << kWordShift) + s_bit;

      uint64_t word = bid_bitmap_[word_idx];
      int bit = kWordMask - __builtin_clzll(word);

      cached_best_bid_idx_ = (word_idx << kWordShift) + bit;
      return cached_best_bid_idx_;
    }
    cached_best_bid_idx_ = -1;
    return -1;
  }

  [[nodiscard]] int best_ask_idx() const noexcept {
    if (cached_best_ask_idx_ >= 0 &&
        ask_orders_[cached_best_ask_idx_].is_active()) {
      return cached_best_ask_idx_;
    }

    const int summary_words = static_cast<int>(ask_summary_.size());
    for (int sw = 0; sw < summary_words; ++sw) {
      uint64_t s_word = ask_summary_[sw];
      if (!s_word)
        continue;

      int s_bit = __builtin_ctzll(s_word);
      int word_idx = (sw << kWordShift) + s_bit;

      uint64_t word = ask_bitmap_[word_idx];
      int bit = __builtin_ctzll(word);

      cached_best_ask_idx_ = (word_idx << kWordShift) + bit;
      return cached_best_ask_idx_;
    }
    cached_best_ask_idx_ = -1;
    return -1;
  }

  [[nodiscard]] common::PriceType best_bid_price() const noexcept {
    const int idx = best_bid_idx();
    return (idx >= 0) ? config_.index_to_price(idx) : common::PriceType{};
  }

  [[nodiscard]] common::PriceType best_ask_price() const noexcept {
    const int idx = best_ask_idx();
    return (idx >= 0) ? config_.index_to_price(idx) : common::PriceType{};
  }

  [[nodiscard]] common::QtyType best_bid_qty() const noexcept {
    const int idx = best_bid_idx();
    if (idx < 0)
      return common::QtyType{};
    return bid_orders_[idx].qty;
  }

  [[nodiscard]] common::QtyType best_ask_qty() const noexcept {
    const int idx = best_ask_idx();
    if (idx < 0)
      return common::QtyType{};
    return ask_orders_[idx].qty;
  }

  void trade_order(const MarketData* market_update, const int idx) {
    if (market_update->side == common::Side::kBuy) {
      if (bid_orders_[idx].is_active()) {
        bid_orders_[idx].qty -= market_update->qty;
        if (!bid_orders_[idx].is_active()) {
          update_bid(idx, common::QtyType::from_raw(0));
        }
        if (idx != cached_best_bid_idx_ &&
            is_bid_idx_valid(cached_best_bid_idx_)) {
          bbo_.bid_qty = bid_orders_[cached_best_bid_idx_].qty;
        } else {
          bbo_.bid_price = best_bid_price();
          bbo_.bid_qty = best_bid_qty();
        }
      }
    } else {
      if (ask_orders_[idx].is_active()) {
        ask_orders_[idx].qty -= market_update->qty;
        if (!ask_orders_[idx].is_active()) {
          update_ask(idx, common::QtyType::from_raw(0));
        }
        if (idx != cached_best_ask_idx_ &&
            is_ask_idx_valid(cached_best_ask_idx_)) {
          bbo_.ask_qty = ask_orders_[cached_best_ask_idx_].qty;
        } else {
          bbo_.ask_price = best_ask_price();
          bbo_.ask_qty = best_ask_qty();
        }
      }
    }
  }

  void delete_order(const MarketData* market_update, const int idx) {
    if (market_update->side == common::Side::kBuy) {
      update_bid(idx, common::QtyType::from_raw(0));
      if (idx != cached_best_bid_idx_ &&
          is_bid_idx_valid(cached_best_bid_idx_)) {
      } else {
        bbo_.bid_price = best_bid_price();
        bbo_.bid_qty = best_bid_qty();
      }
    } else {
      update_ask(idx, common::QtyType::from_raw(0));
      if (idx != cached_best_ask_idx_ &&
          is_ask_idx_valid(cached_best_ask_idx_)) {
      } else {
        bbo_.ask_price = best_ask_price();
        bbo_.ask_qty = best_ask_qty();
      }
    }
  }

  [[nodiscard]] bool is_bid_idx_valid(int idx) const noexcept {
    return idx >= 0 && bid_orders_[idx].is_active();
  }

  [[nodiscard]] bool is_ask_idx_valid(int idx) const noexcept {
    return idx >= 0 && ask_orders_[idx].is_active();
  }

  void add_order(const MarketData* market_update, const int idx,
      const common::QtyType qty) {
    if (market_update->side == common::Side::kBuy) {
      update_bid(idx, qty);
      if (idx > cached_best_bid_idx_) {
        cached_best_bid_idx_ = idx;
      }
      if (is_bid_idx_valid(cached_best_bid_idx_)) {
        bbo_.bid_price = config_.index_to_price(cached_best_bid_idx_);
        bbo_.bid_qty = bid_orders_[cached_best_bid_idx_].qty;
      } else {
        bbo_.bid_price = best_bid_price();
        bbo_.bid_qty = best_bid_qty();
      }
    } else {
      update_ask(idx, qty);
      if (cached_best_ask_idx_ < 0 || idx < cached_best_ask_idx_) {
        cached_best_ask_idx_ = idx;
      }
      if (is_ask_idx_valid(cached_best_ask_idx_)) {
        bbo_.ask_price = config_.index_to_price(cached_best_ask_idx_);
        bbo_.ask_qty = ask_orders_[cached_best_ask_idx_].qty;
      } else {
        bbo_.ask_price = best_ask_price();
        bbo_.ask_qty = best_ask_qty();
      }
    }
  }
};

#else   // !USE_FLAT_ORDERBOOK
// ============================================================================
// Bucket based OrderBook (default)
// ============================================================================
template <typename Strategy>
class MarketOrderBook final {
 public:
  MarketOrderBook(const common::TickerId& ticker_id,
      const common::Logger::Producer& logger)
      : ticker_id_(std::move(ticker_id)),
        logger_(logger),
        config_(OrderBookConfig::from_ini()),
        bid_buckets_(config_.bucket_count, nullptr),
        ask_buckets_(config_.bucket_count, nullptr),
        bid_summary_(config_.summary_words, 0),
        ask_summary_(config_.summary_words, 0),
        bid_bucket_pool_(std::make_unique<common::MemoryPool<Bucket>>(
            config_.bucket_pool_size)),
        ask_bucket_pool_(std::make_unique<common::MemoryPool<Bucket>>(
            config_.bucket_pool_size)) {
    LOG_INFO(logger_,
        "[Constructor] MarketOrderBook Created - min_price: {}, max_price: {}, "
        "bucket_count: {}, summary_words: {}",
        config_.min_price_int,
        config_.max_price_int,
        config_.bucket_count,
        config_.summary_words);
  }

  ~MarketOrderBook() {
    std::cout << "[Destructor] MarketOrderBook Destroy\n";
    trade_engine_ = nullptr;
  }

  auto on_market_data_updated(
      const MarketData* market_update) noexcept -> void {

    const int64_t max_price_raw = config_.max_price_int;
    const int64_t min_price_raw = config_.min_price_int;

    if (market_update->price.value > max_price_raw ||
        market_update->price.value < min_price_raw) {
      LOG_ERROR(logger_,
          "common::Price[{}] is invalid (range: {} ~ {})",
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
        LOG_ERROR(logger_, "error in market update data");
        break;
      case common::MarketUpdateType::kClear: {
        for (int i = 0; i < config_.bucket_count; ++i) {
          if (bid_buckets_[i]) {
            bid_bucket_pool_->deallocate(bid_buckets_[i]);
            bid_buckets_[i] = nullptr;
          }
          if (ask_buckets_[i]) {
            ask_bucket_pool_->deallocate(ask_buckets_[i]);
            ask_buckets_[i] = nullptr;
          }
        }
        std::fill(bid_summary_.begin(), bid_summary_.end(), 0);
        std::fill(ask_summary_.begin(), ask_summary_.end(), 0);
        bbo_ = {.bid_price = common::PriceType{},  // kInvalidValue
            .ask_price = common::PriceType{},      // kInvalidValue
            .bid_qty = common::QtyType{},          // kInvalidValue
            .ask_qty = common::QtyType{}};         // kInvalidValue
        logger_.info("Cleared all market data.");
        return;
      }
      case common::MarketUpdateType::kAdd:
      case common::MarketUpdateType::kModify: {
        START_MEASURE(ORDERBOOK_APPLY);
        add_order(market_update, idx, qty);
        END_MEASURE(ORDERBOOK_APPLY, logger_);
        break;
      }
      case common::MarketUpdateType::kCancel: {
        START_MEASURE(ORDERBOOK_APPLY);
        delete_order(market_update, idx);
        END_MEASURE(ORDERBOOK_APPLY, logger_);
        break;
      }
      case common::MarketUpdateType::kTrade: {
        if (LIKELY(trade_engine_)) {
          trade_engine_->on_trade_updated(market_update, this);
        }
        return;
      }
      case common::MarketUpdateType::kBookTicker: {
        if (LIKELY(trade_engine_)) {
          trade_engine_->on_book_ticker_updated(market_update);
        }
        return;
      }
    }

    LOG_TRACE(logger_,
        "[Updated] {} {}",
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
    const auto& buckets = is_bid ? bid_buckets_ : ask_buckets_;

    for (int bucket_idx = 0; bucket_idx < config_.bucket_count; ++bucket_idx) {
      const Bucket* bucket = buckets[bucket_idx];
      if (!bucket)
        continue;

      for (int off = 0; off < kBucketSize; ++off) {
        const MarketOrder& order = bucket->orders[off];
        if (order.is_active()) {
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
    return next_active_impl<SearchHighest>(bid_summary_,
        bid_buckets_,
        start_idx);
  }

  [[nodiscard]] int next_active_ask(int start_idx) const noexcept {
    return next_active_impl<SearchLowest>(ask_summary_,
        ask_buckets_,
        start_idx);
  }

 private:
  template <typename SearchDir>
  [[nodiscard]] int next_active_impl(
      const std::vector<uint64_t>& summary_bitmap,
      const std::vector<Bucket*>& buckets, int start_idx) const noexcept {
    const int bucket_index = start_idx / kBucketSize;
    const int level_offset = start_idx & (kBucketSize - 1);

    if (const Bucket* bucket = buckets[bucket_index]) {
      const int word_index = level_offset >> kWordShift;
      const int bit_offset = level_offset & kWordMask;

      const uint64_t mask = SearchDir::make_mask(bit_offset);
      if (const uint64_t word = bucket->bitmap[word_index] & mask) {
        const int bit_index = SearchDir::find_bit(word);
        return bucket_index * kBucketSize +
               (word_index * kBitsPerWord + bit_index);
      }

      // Search remaining words in bucket
      int result = -1;
      SearchDir::iterate_words(word_index,
          SearchDir::kIsHighest ? 0 : kBucketBitmapWords,
          [&](int word_idx) {
            if (const uint64_t word = bucket->bitmap[word_idx]) {
              const int bit_index = SearchDir::find_bit(word);
              result = bucket_index * kBucketSize +
                       (word_idx * kBitsPerWord + bit_index);
              return true;
            }
            return false;
          });
      if (result >= 0)
        return result;
    }

    const int summary_word_index = bucket_index >> kWordShift;
    const int summary_bit_offset = bucket_index & kWordMask;
    const uint64_t sb_word = summary_bitmap[summary_word_index] &
                             SearchDir::make_mask(summary_bit_offset);

    if (sb_word) {
      const int bit = SearchDir::find_bit(sb_word);
      const int next_bucket_index = (summary_word_index << kWordShift) + bit;
      const int off_in_bucket =
          find_in_bucket(buckets[next_bucket_index], SearchDir::kIsHighest);
      return next_bucket_index * kBucketSize + off_in_bucket;
    }

    int result = -1;
    SearchDir::iterate_summary(summary_word_index,
        config_.summary_words,
        [&](int swi) {
          if (const uint64_t summary_word = summary_bitmap[swi]) {
            const int bit = SearchDir::find_bit(summary_word);
            const int next_bucket_index = (swi << kWordShift) + bit;
            const int off_in_bucket = find_in_bucket(buckets[next_bucket_index],
                SearchDir::kIsHighest);
            result = next_bucket_index * kBucketSize + off_in_bucket;
            return true;
          }
          return false;
        });
    return result;
  }

 public:
  [[nodiscard]] std::vector<int> peek_levels(bool is_bid, int level) const {
    std::vector<int> output;
    int idx = is_bid ? best_bid_idx() : best_ask_idx();
    if (idx >= 0) {
      output.push_back(idx);
    }
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
          const int bit = kWordMask - std::countl_zero(word);
          return iter * kBitsPerWord + bit;
        }
      }
    } else {
      for (int iter = 0; iter < kBucketBitmapWords; ++iter) {
        if (const uint64_t word = bucket->bitmap[iter]) {
          const int bit = std::countr_zero(word);
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

    const auto& summary = is_bid ? bid_summary_ : ask_summary_;
    const auto& buckets = is_bid ? bid_buckets_ : ask_buckets_;

    const int best_idx = is_bid ? best_bid_idx() : best_ask_idx();
    if (best_idx < 0)
      return 0;

    const int best_bucket_idx = best_idx / kBucketSize;
    const int best_off = best_idx & (kBucketSize - 1);
    const Bucket* best_bucket = buckets[best_bucket_idx];

    if (best_bucket && best_bucket->orders[best_off].is_active()) {
      out.push_back(LevelView{best_idx,
          best_bucket->orders[best_off].qty.value,
          config_.index_to_price(best_idx).value});
      if (static_cast<int>(out.size()) >= level)
        return static_cast<int>(out.size());
    }

    int bucket_idx = best_bucket_idx;
    int off = best_off;

    auto price_of = [this](int gidx) -> common::PriceType {
      return config_.index_to_price(gidx);
    };

    auto consume_full_bucket = [&](int bidx) {
      const Bucket* bucket = buckets[bidx];
      if (!bucket)
        return false;

      auto on_off = [&](int loc_off) {
        return push_level_if_positive(bucket,
            bidx,
            loc_off,
            price_of,
            out,
            level);
      };

      if (is_bid) {
        for (int wi = kBucketBitmapWords - 1;
             wi >= 0 && static_cast<int>(out.size()) < level;
             --wi) {
          if (scan_word<true>(bucket->bitmap[wi], wi, on_off))
            return true;
        }
      } else {
        for (int wi = 0;
             wi < kBucketBitmapWords && static_cast<int>(out.size()) < level;
             ++wi) {
          if (scan_word<false>(bucket->bitmap[wi], wi, on_off))
            return true;
        }
      }
      return static_cast<int>(out.size()) >= level;
    };

    {
      const Bucket* bucket = buckets[bucket_idx];
      if (bucket) {
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
      }
    }

    while (static_cast<int>(out.size()) < level) {
      bucket_idx = is_bid ? jump_next_bucket_impl<true>(summary, bucket_idx)
                          : jump_next_bucket_impl<false>(summary, bucket_idx);
      if (bucket_idx < 0)
        break;
      if (consume_full_bucket(bucket_idx))
        break;
    }

    return static_cast<int>(out.size());
  }

  template <typename T>
  [[nodiscard]] int peek_qty(bool is_bid, int level, std::span<T> qty_out,
      std::span<int> idx_out) const noexcept {
    const auto want = std::min<int>(level, static_cast<int>(qty_out.size()));
    if (want <= 0)
      return -1;

    const auto& summary = is_bid ? bid_summary_ : ask_summary_;
    const auto& buckets = is_bid ? bid_buckets_ : ask_buckets_;

    int filled = 0;

    const int best_idx = is_bid ? best_bid_idx() : best_ask_idx();
    if (best_idx < 0)
      return -1;

    const int best_bidx = best_idx / kBucketSize;
    const int best_off = best_idx & (kBucketSize - 1);
    const Bucket* best_bucket = buckets[best_bidx];

    if (best_bucket && best_bucket->orders[best_off].is_active()) {
      if constexpr (std::is_same_v<T, int64_t>) {
        qty_out[filled] = best_bucket->orders[best_off].qty_raw();
      } else {
        qty_out[filled] = best_bucket->orders[best_off].qty_as_double();
      }
      if (!idx_out.empty())
        idx_out[filled] = best_idx;
      ++filled;
      if (filled >= want)
        return filled;
    }

    auto consume_bucket_after_best = [&](int bidx, int start_off) {
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

    int bidx = best_bidx;
    const int off = best_off;

    if (consume_bucket_after_best(bidx, off))
      return filled;

    auto consume_full_bucket = [&](int bidx) {
      const Bucket* bucket = buckets[bidx];
      if (!bucket)
        return false;

      auto on_off = [&](int off) {
        return push_if_active(bucket,
            bidx,
            off,
            qty_out,
            idx_out,
            filled,
            want);
      };

      if (is_bid) {
        for (int wi = kBucketBitmapWords - 1; wi >= 0 && filled < want; --wi) {
          if (scan_word<true>(bucket->bitmap[wi], wi, on_off))
            return true;
        }
      } else {
        for (int wi = 0; wi < kBucketBitmapWords && filled < want; ++wi) {
          if (scan_word<false>(bucket->bitmap[wi], wi, on_off))
            return true;
        }
      }
      return filled >= want;
    };

    // NOLINTBEGIN(bugprone-infinite-loop)
    while (filled < want) {
      bidx = jump_next_bucket(bidx);
      if (bidx < 0)
        break;
      if (consume_full_bucket(bidx))
        break;
    }
    // NOLINTEND(bugprone-infinite-loop)

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

  std::vector<Bucket*> bid_buckets_;
  std::vector<Bucket*> ask_buckets_;
  std::vector<uint64_t> bid_summary_;
  std::vector<uint64_t> ask_summary_;

  mutable int cached_best_bid_idx_ = -1;
  mutable int cached_best_ask_idx_ = -1;

  BBO bbo_;

  std::unique_ptr<common::MemoryPool<Bucket>> bid_bucket_pool_;
  std::unique_ptr<common::MemoryPool<Bucket>> ask_bucket_pool_;

  void update_bid(int idx, common::QtyType qty) {
    const int bucket_idx = idx / kBucketSize;
    const int off = idx & (kBucketSize - 1);

    if (!bid_buckets_[bucket_idx]) {
      bid_buckets_[bucket_idx] = bid_bucket_pool_->allocate();
      std::ranges::fill(bid_buckets_[bucket_idx]->bitmap, 0);
      for (auto& order : bid_buckets_[bucket_idx]->orders) {
        order.qty = common::QtyType::from_raw(0);
      }
    }
    Bucket* const bucket = bid_buckets_[bucket_idx];

    auto& order = bucket->orders[off];
    order.qty = qty;

    const int word = off >> kWordShift;
    const uint64_t mask = (1ULL << (off & kWordMask));

    if (order.is_active()) {
      bucket->bitmap[word] |= mask;
      set_summary(true, bucket_idx);
    } else {
      bucket->bitmap[word] &= ~mask;
      if (bucket->empty()) {
        clear_summary(true, bucket_idx);
        bid_bucket_pool_->deallocate(bucket);
        bid_buckets_[bucket_idx] = nullptr;
      }
    }
  }

  void update_ask(const int idx, const common::QtyType qty) {
    const int bidx = idx / kBucketSize;
    const int off = idx & (kBucketSize - 1);

    if (!ask_buckets_[bidx]) {
      ask_buckets_[bidx] = ask_bucket_pool_->allocate();
      std::ranges::fill(ask_buckets_[bidx]->bitmap, 0);
      for (auto& order : ask_buckets_[bidx]->orders) {
        order.qty = common::QtyType::from_raw(0);
      }
    }
    Bucket* bucket = ask_buckets_[bidx];

    auto& order = bucket->orders[off];
    order.qty = qty;

    const int word = off >> kWordShift;
    const uint64_t mask = (1ULL << (off & kWordMask));

    if (order.is_active()) {
      bucket->bitmap[word] |= mask;
      set_summary(false, bidx);
    } else {
      bucket->bitmap[word] &= ~mask;
      if (bucket->empty()) {
        clear_summary(false, bidx);
        ask_bucket_pool_->deallocate(bucket);
        ask_buckets_[bidx] = nullptr;
      }
    }
  }

  [[nodiscard]] bool is_bid_idx_valid(int idx) const noexcept {
    if (idx < 0)
      return false;
    const int bucket_idx = idx / kBucketSize;
    const int offset = idx & (kBucketSize - 1);
    const Bucket* bucket = bid_buckets_[bucket_idx];
    return (bucket != nullptr) && bucket->orders[offset].is_active();
  }

  [[nodiscard]] bool is_ask_idx_valid(int idx) const noexcept {
    if (idx < 0)
      return false;
    const int bucket_idx = idx / kBucketSize;
    const int offset = idx & (kBucketSize - 1);
    const Bucket* bucket = ask_buckets_[bucket_idx];
    return (bucket != nullptr) && bucket->orders[offset].is_active();
  }

  [[nodiscard]] int best_bid_idx() const noexcept {
    // Fast path: cached index is still valid
    if (cached_best_bid_idx_ >= 0 && is_bid_idx_valid(cached_best_bid_idx_)) {
      return cached_best_bid_idx_;
    }

    // Slow path: 2-level search via summary
    for (int sw = config_.summary_words - 1; sw >= 0; --sw) {
      const uint64_t word = bid_summary_[sw];
      if (!word)
        continue;

      const int bit = kWordMask - std::countl_zero(word);
      const int bidx = (sw << kWordShift) + bit;
      Bucket* bucket = bid_buckets_[bidx];
      assert(bucket);

      for (int lw = kBucketBitmapWords - 1; lw >= 0; --lw) {
        const uint64_t lword = bucket->bitmap[lw];
        if (!lword)
          continue;

        const int lbit = kWordMask - std::countl_zero(lword);
        cached_best_bid_idx_ = bidx * kBucketSize + lw * kBitsPerWord + lbit;
        return cached_best_bid_idx_;
      }
    }
    cached_best_bid_idx_ = -1;
    return -1;
  }

  [[nodiscard]] int best_ask_idx() const noexcept {
    // Fast path: cached index is still valid
    if (cached_best_ask_idx_ >= 0 && is_ask_idx_valid(cached_best_ask_idx_)) {
      return cached_best_ask_idx_;
    }

    // Slow path: 2-level search via summary
    for (int sw = 0; sw < config_.summary_words; ++sw) {
      const uint64_t word = ask_summary_[sw];
      if (!word)
        continue;

      const int bit = std::countr_zero(word);
      const int bidx = (sw << kWordShift) + bit;
      Bucket* bucket = ask_buckets_[bidx];
      assert(bucket);

      for (int lw = 0; lw < kBucketBitmapWords; ++lw) {
        const uint64_t lword = bucket->bitmap[lw];
        if (!lword)
          continue;

        const int lbit = std::countr_zero(lword);
        cached_best_ask_idx_ = bidx * kBucketSize + lw * kBitsPerWord + lbit;
        return cached_best_ask_idx_;
      }
    }
    cached_best_ask_idx_ = -1;
    return -1;
  }

  [[nodiscard]] common::PriceType best_bid_price() const noexcept {
    const int idx = best_bid_idx();
    return (idx >= 0) ? config_.index_to_price(idx) : common::PriceType{};
  }

  [[nodiscard]] common::PriceType best_ask_price() const noexcept {
    const int idx = best_ask_idx();
    return (idx >= 0) ? config_.index_to_price(idx) : common::PriceType{};
  }

  [[nodiscard]] common::QtyType best_bid_qty() const noexcept {
    const int idx = best_bid_idx();
    if (idx < 0)
      return common::QtyType{};
    const int bidx = idx / kBucketSize;
    const int off = idx & (kBucketSize - 1);
    Bucket* bucket = bid_buckets_[bidx];
    return bucket ? bucket->orders[off].qty : common::QtyType::from_raw(0);
  }

  [[nodiscard]] common::QtyType best_ask_qty() const noexcept {
    const int idx = best_ask_idx();
    if (idx < 0)
      return common::QtyType{};
    const int bidx = idx / kBucketSize;
    const int off = idx & (kBucketSize - 1);
    Bucket* bucket = ask_buckets_[bidx];
    return bucket ? bucket->orders[off].qty : common::QtyType::from_raw(0);
  }

  void trade_order(const MarketData* market_update, const int idx) {
    const int bidx = idx / kBucketSize;
    const int off = idx & (kBucketSize - 1);

    if (market_update->side == common::Side::kBuy) {
      Bucket* bucket = bid_buckets_[bidx];
      if (bucket && bucket->orders[off].is_active()) {
        bucket->orders[off].qty -= market_update->qty;
        const bool was_depleted = bucket->orders[off].qty.value <= 0;
        if (was_depleted) {
          update_bid(idx, common::QtyType::from_raw(0));
        }
        if (was_depleted && idx == cached_best_bid_idx_) {
          cached_best_bid_idx_ = -1;  // Invalidate before recalc
          bbo_.bid_price = best_bid_price();
          bbo_.bid_qty = best_bid_qty();
        } else if (idx == cached_best_bid_idx_) {
          // Same level, just qty changed
          bbo_.bid_qty = bucket->orders[off].qty;
        }
      }
    } else {
      Bucket* bucket = ask_buckets_[bidx];
      if (bucket && bucket->orders[off].is_active()) {
        bucket->orders[off].qty -= market_update->qty;
        const bool was_depleted = bucket->orders[off].qty.value <= 0;
        if (was_depleted) {
          update_ask(idx, common::QtyType::from_raw(0));
        }
        if (was_depleted && idx == cached_best_ask_idx_) {
          cached_best_ask_idx_ = -1;
          bbo_.ask_price = best_ask_price();
          bbo_.ask_qty = best_ask_qty();
        } else if (idx == cached_best_ask_idx_) {
          bbo_.ask_qty = bucket->orders[off].qty;
        }
        // else: traded level wasn't best, no BBO change
      }
    }
  }

  void delete_order(const MarketData* market_update, const int idx) {
    if (market_update->side == common::Side::kBuy) {
      update_bid(idx, common::QtyType::from_raw(0));
      if (idx == cached_best_bid_idx_) {
        cached_best_bid_idx_ = -1;
        bbo_.bid_price = best_bid_price();
        bbo_.bid_qty = best_bid_qty();
      }
    } else {
      update_ask(idx, common::QtyType::from_raw(0));
      if (idx == cached_best_ask_idx_) {
        cached_best_ask_idx_ = -1;
        bbo_.ask_price = best_ask_price();
        bbo_.ask_qty = best_ask_qty();
      }
    }
  }

  void add_order(const MarketData* market_update, const int idx,
      const common::QtyType qty) {
    if (market_update->side == common::Side::kBuy) {
      update_bid(idx, qty);
      if (cached_best_bid_idx_ < 0 || idx > cached_best_bid_idx_) {
        cached_best_bid_idx_ = idx;
        bbo_.bid_price = config_.index_to_price(idx);
        bbo_.bid_qty = qty;
      } else if (idx == cached_best_bid_idx_) {
        bbo_.bid_qty = qty;
      }
    } else {
      update_ask(idx, qty);
      if (cached_best_ask_idx_ < 0 || idx < cached_best_ask_idx_) {
        cached_best_ask_idx_ = idx;
        bbo_.ask_price = config_.index_to_price(idx);
        bbo_.ask_qty = qty;
      } else if (idx == cached_best_ask_idx_) {
        bbo_.ask_qty = qty;
      }
    }
  }

  void set_summary(bool is_bid, int bid_x) noexcept {
    auto& summary = is_bid ? bid_summary_ : ask_summary_;
    summary[bid_x >> kWordShift] |= (1ULL << (bid_x & kWordMask));
  }

  void clear_summary(bool is_bid, int bid_x) noexcept {
    auto& summary = is_bid ? bid_summary_ : ask_summary_;
    summary[bid_x >> kWordShift] &= ~(1ULL << (bid_x & kWordMask));
  }
};
#endif  // USE_FLAT_ORDERBOOK

template <typename Strategy>
using MarketOrderBookHashMap =
    std::map<std::string, std::unique_ptr<MarketOrderBook<Strategy>>>;
}  // namespace trading

#endif  // ORDERBOOK_HPP
