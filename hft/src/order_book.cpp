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

#include "order_book.h"

#include "market_data.h"
#include "trade_engine.h"

using common::MarketUpdateType;
using common::Price;
using common::Qty;
using common::Side;
using common::TickerId;

constexpr int kBucketPoolSize = 1024 * 8;

namespace trading {
MarketOrder::MarketOrder() = default;

MarketOrder::MarketOrder(const Qty qty_, const bool active_ = false) noexcept
    : qty(qty_), active(active_) {}

auto MarketOrder::toString() const -> std::string {
  std::stringstream stream;
  stream << "[MarketOrder]" << "[" << "qty:" << qty.value << " "
         << "active:" << active << " ";

  return stream.str();
}

MarketOrderBook::MarketOrderBook(const TickerId& ticker_id,
                                 common::Logger* logger)
    : ticker_id_(std::move(ticker_id)),
      logger_(logger),
      bid_bucket_pool_(
          std::make_unique<common::MemoryPool<Bucket>>(kBucketPoolSize)),
      ask_bucket_pool_(
          std::make_unique<common::MemoryPool<Bucket>>(kBucketPoolSize)) {}

MarketOrderBook::~MarketOrderBook() {
  logger_->info("MarketOrderBook::~MarketOrderBook");

  trade_engine_ = nullptr;
  logger_ = nullptr;
}

void MarketOrderBook::update_bid(int idx, Qty qty) {
  const int bucket_idx = idx / kBucketSize;
  const int off = idx & (kBucketSize - 1);

  if (!bidBuckets_[bucket_idx]) {
    bidBuckets_[bucket_idx] = bid_bucket_pool_->allocate();
    std::ranges::fill(bidBuckets_[bucket_idx]->bitmap, 0);
    for (auto& order : bidBuckets_[bucket_idx]->orders) {
      order.active = false;
      order.qty = Qty{.0};
    }
  }
  Bucket* const bucket = bidBuckets_[bucket_idx];

  auto& order = bucket->orders[off];
  order.qty = qty;
  order.active = (qty.value > .0);

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

void MarketOrderBook::update_ask(const int idx, const Qty qty) {
  const int bidx = idx / kBucketSize;
  const int off = idx & (kBucketSize - 1);

  if (!askBuckets_[bidx]) {
    askBuckets_[bidx] = ask_bucket_pool_->allocate();
    std::ranges::fill(askBuckets_[bidx]->bitmap, 0);
    for (auto& order : askBuckets_[bidx]->orders) {
      order.active = false;
      order.qty = Qty{.0};
    }
  }
  Bucket* bucket = askBuckets_[bidx];

  auto& order = bucket->orders[off];
  order.qty = qty;
  order.active = (qty.value > .0);

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

int MarketOrderBook::best_bid_idx() const noexcept {
  for (int sw = kSummaryWords - 1; sw >= 0; --sw) {
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

int MarketOrderBook::best_ask_idx() const noexcept {
  for (int sw = 0; sw < kSummaryWords; ++sw) {
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

Price MarketOrderBook::best_bid_price() const noexcept {
  const int idx = best_bid_idx();
  return (idx >= 0) ? indexToPrice(idx) : Price{common::kPriceInvalid};
}

Price MarketOrderBook::best_ask_price() const noexcept {
  const int idx = best_ask_idx();
  return (idx >= 0) ? indexToPrice(idx) : Price{common::kPriceInvalid};
}

Qty MarketOrderBook::best_bid_qty() const noexcept {
  const int idx = best_bid_idx();
  if (idx < 0)
    return Qty{common::kQtyInvalid};
  const int bidx = idx / kBucketSize;
  const int off = idx & (kBucketSize - 1);
  Bucket* bucket = bidBuckets_[bidx];
  return bucket ? bucket->orders[off].qty : Qty{common::kQtyInvalid};
}

Qty MarketOrderBook::best_ask_qty() const noexcept {
  const int idx = best_ask_idx();
  if (idx < 0)
    return Qty{common::kQtyInvalid};
  const int bidx = idx / kBucketSize;
  const int off = idx & (kBucketSize - 1);
  Bucket* bucket = askBuckets_[bidx];
  return bucket ? bucket->orders[off].qty : Qty{common::kQtyInvalid};
}

void MarketOrderBook::trade_order(const MarketData* market_update,
                                  const int idx) {
  const int bidx = idx / kBucketSize;
  const int off = idx & (kBucketSize - 1);

  if (market_update->side == Side::kBuy) {
    Bucket* bucket = bidBuckets_[bidx];
    if (bucket && bucket->orders[off].active) {
      bucket->orders[off].qty.value -= market_update->qty.value;
      if (bucket->orders[off].qty.value <= 0.) {
        update_bid(idx, Qty{0.});
      }
      bbo_.bid_price = best_bid_price();
      bbo_.bid_qty = best_bid_qty();
    }
  } else {
    Bucket* bucket = askBuckets_[bidx];
    if (bucket && bucket->orders[off].active) {
      bucket->orders[off].qty.value -= market_update->qty.value;
      if (bucket->orders[off].qty.value <= 0.) {
        update_ask(idx, Qty{0.});
      }
      bbo_.ask_price = best_ask_price();
      bbo_.ask_qty = best_ask_qty();
    }
  }
}

void MarketOrderBook::delete_order(const MarketData* market_update,
                                   const int idx) {
  if (market_update->side == Side::kBuy) {
    update_bid(idx, Qty{0.});  // 비활성화
    bbo_.bid_price = best_bid_price();
    bbo_.bid_qty = best_bid_qty();
  } else {
    update_ask(idx, Qty{0.});
    bbo_.ask_price = best_ask_price();
    bbo_.ask_qty = best_ask_qty();
  }
}

void MarketOrderBook::add_order(const MarketData* market_update, const int idx,
                                const Qty qty) {
  if (market_update->side == Side::kBuy) {
    update_bid(idx, qty);
    bbo_.bid_price = best_bid_price();
    bbo_.bid_qty = best_bid_qty();
  } else {
    update_ask(idx, qty);
    bbo_.ask_price = best_ask_price();
    bbo_.ask_qty = best_ask_qty();
  }
}

/// Process market data update and update the limit order book.
auto MarketOrderBook::on_market_data_updated(
    const MarketData* market_update) noexcept -> void {
  const int idx = priceToIndex(market_update->price);
  const Qty qty = market_update->qty;

  switch (market_update->type) {
    case MarketUpdateType::kAdd:
    case MarketUpdateType::kModify: {
      add_order(market_update, idx, qty);
      break;
    }
    case MarketUpdateType::kCancel: {
      delete_order(market_update, idx);
      break;
    }
    case MarketUpdateType::kTrade: {
      trade_order(market_update, idx);

      if (trade_engine_) {
        trade_engine_->on_trade_updated(market_update, this);
      }
      return;  // trade만 예외 처리
    }
    case MarketUpdateType::kClear: {
      for (int i = 0; i < kBucketCount; ++i) {
        if (bidBuckets_[i]) {
          bid_bucket_pool_->deallocate(bidBuckets_[i]);
          bidBuckets_[i] = nullptr;
        }
        if (askBuckets_[i]) {
          ask_bucket_pool_->deallocate(askBuckets_[i]);
          askBuckets_[i] = nullptr;
        }
      }
      bidSummary_.fill(0);
      askSummary_.fill(0);
      bbo_ = {.bid_price = Price{common::kPriceInvalid},
              .ask_price = Price{common::kPriceInvalid},
              .bid_qty = Qty{common::kQtyInvalid},
              .ask_qty = Qty{common::kQtyInvalid}};
      break;
    }
    case MarketUpdateType::kInvalid:
      logger_->error("error in market update data");
      break;
  }

  logger_->debug(std::format("{}:{} {}() {} {}\n", __FILE__, __LINE__,
                             __FUNCTION__, market_update->toString(),
                             bbo_.toString()));

  trade_engine_->on_order_book_updated(market_update->price,
                                       market_update->side, this);
}

auto MarketOrderBook::get_bbo() noexcept -> const BBO* {
  return &bbo_;
}

void MarketOrderBook::on_trade_update(MarketData*) {}

std::string MarketOrderBook::print_active_levels(bool is_bid) const {
  std::stringstream stream;
  const auto& buckets = is_bid ? bidBuckets_ : askBuckets_;

  for (int bucket_idx = 0; bucket_idx < kBucketCount; ++bucket_idx) {
    const Bucket* bucket = buckets[bucket_idx];
    if (!bucket)
      continue;

    for (int off = 0; off < kBucketSize; ++off) {
      const MarketOrder& order = bucket->orders[off];
      if (order.active && order.qty.value > 0.) {
        const int global_idx = bucket_idx * kBucketSize + off;
        const Price price = indexToPrice(global_idx);
        stream << (is_bid ? "[BID]" : "[ASK]") << " idx:" << global_idx
               << " px:" << common::toString(price)
               << " qty:" << common::toString(order.qty) << "\n";
      }
    }
  }

  return stream.str();
}

int MarketOrderBook::next_active_bid(const int start_idx) const noexcept {
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

int MarketOrderBook::next_active_ask(const int start_idx) const noexcept {
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
  const uint64_t sb_word = summary_bitmap[summary_word_index] &
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

  for (int iter = summary_word_index + 1; iter < kSummaryWords; ++iter) {
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

std::vector<int> MarketOrderBook::peek_levels(const bool is_bid,
                                              const int level) const {
  std::vector<int> output;
  int idx = is_bid ? best_bid_idx() : best_ask_idx();
  while (idx >= 0 && output.size() < static_cast<size_t>(level)) {
    idx = next_active_idx(is_bid, idx);
    if (idx >= 0)
      output.push_back(idx);
  }
  return output;
}

// highest=true  => 버킷 내에서 가장 큰(High‑우선) 레벨 오프셋
// highest=false => 버킷 내에서 가장 작은(Low‑우선) 레벨 오프셋
int MarketOrderBook::find_in_bucket(const Bucket* bucket,
                                    const bool highest) noexcept {
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
}  // namespace trading