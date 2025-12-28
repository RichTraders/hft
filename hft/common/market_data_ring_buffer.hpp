/*
 * MIT License
 *
 * Copyright (c) 2025 NewOro Corporation
 *
 * Permission is hereby granted, free of charge, to use, copy, modify, and
 * distribute this software for any purpose with or without fee, provided that
 * the above copyright notice appears in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 */

#ifndef MARKET_DATA_RING_BUFFER_HPP
#define MARKET_DATA_RING_BUFFER_HPP

#include <common/types.h>
#include <common/var_length_ring_buffer.hpp>

namespace common {

// 버퍼 크기 상수
constexpr size_t kTradeBufferSize = 64UL * 1024UL;             // 64KB
constexpr size_t kDepthBufferSize = 1UL * 1024UL * 1024UL;     // 1MB
constexpr size_t kSnapshotBufferSize = 4UL * 1024UL * 1024UL;  // 4MB

// Depth/Snapshot 메타데이터
struct DepthMeta {
  uint64_t start_idx;
  uint64_t end_idx;
  uint64_t prev_end_idx;
};

// MarketData 엔트리 (간소화된 버전, pool 없이 직접 저장)
struct MarketDataEntry {
  MarketUpdateType type;
  Side side;
  Price price;
  Qty qty;
  // TickerId는 메타데이터에서 공유 (모든 엔트리가 같은 심볼)
};

/**
 * 타입별 Market Data 링버퍼
 *
 * Trade, Depth, Snapshot 각각 독립된 버퍼로 관리
 * - Trade: 작은 메시지, 높은 빈도
 * - Depth: 중간 크기, 높은 빈도
 * - Snapshot: 큰 메시지, 낮은 빈도
 */
class MarketDataRingBuffer {
 public:
  MarketDataRingBuffer()
      : trade_buffer_(kTradeBufferSize),
        depth_buffer_(kDepthBufferSize),
        snapshot_buffer_(kSnapshotBufferSize) {}

  // 버퍼 크기 커스텀
  MarketDataRingBuffer(size_t trade_size, size_t depth_size,
      size_t snapshot_size)
      : trade_buffer_(trade_size),
        depth_buffer_(depth_size),
        snapshot_buffer_(snapshot_size) {}

  // ============== Producer API ==============

  /**
   * Trade 쓰기
   */
  [[nodiscard]] bool write_trade(Side side, Price price, Qty qty) noexcept {
    const MarketDataEntry entry{MarketUpdateType::kTrade, side, price, qty};
    return trade_buffer_.write(static_cast<uint16_t>(RingBufferMsgType::kTrade),
        entry,
        1);
  }

  /**
   * BookTicker 쓰기 (bid + ask)
   */
  [[nodiscard]] bool write_book_ticker(Price bid_price, Qty bid_qty,
      Price ask_price, Qty ask_qty) noexcept {
    std::array<MarketDataEntry, 2> entries = {{
        {MarketUpdateType::kBookTicker, Side::kBuy, bid_price, bid_qty},
        {MarketUpdateType::kBookTicker, Side::kSell, ask_price, ask_qty},
    }};
    const DepthMeta meta{0, 0, 0};  // BookTicker는 idx 불필요
    return depth_buffer_.write_var(
        static_cast<uint16_t>(RingBufferMsgType::kBookTicker),
        meta,
        entries.data(),
        entries.size());
  }

  /**
   * Depth 쓰기
   */
  [[nodiscard]] bool write_depth(uint64_t start_idx, uint64_t end_idx,
      uint64_t prev_end_idx, const MarketDataEntry* entries,
      size_t count) noexcept {
    const DepthMeta meta{start_idx, end_idx, prev_end_idx};
    return depth_buffer_.write_var(
        static_cast<uint16_t>(RingBufferMsgType::kDepth),
        meta,
        entries,
        count);
  }

  /**
   * Snapshot 쓰기
   */
  [[nodiscard]] bool write_snapshot(uint64_t update_id,
      const MarketDataEntry* entries, size_t count) noexcept {
    const DepthMeta meta{update_id, update_id, 0};
    return snapshot_buffer_.write_var(
        static_cast<uint16_t>(RingBufferMsgType::kSnapshot),
        meta,
        entries,
        count);
  }

  // ============== Consumer API ==============

  /**
   * Trade 읽기
   * @param handler void(Side side, Price price, Qty qty)
   * @return 읽은 메시지 수
   */
  template <typename Handler>
  size_t read_trade(Handler&& handler) noexcept {
    return trade_buffer_.read([&](uint16_t /*type*/,
                                  uint16_t /*count*/,
                                  void* body,
                                  uint32_t /*len*/) {
      auto* entry = static_cast<MarketDataEntry*>(body);
      handler(entry->side, entry->price, entry->qty);
    });
  }

  /**
   * Depth/BookTicker 읽기
   * @param handler void(uint16_t type, const DepthMeta& meta,
   *                     const MarketDataEntry* entries, size_t count)
   * @return 읽은 메시지 수
   */
  template <typename Handler>
  size_t read_depth(Handler&& handler) noexcept {
    return depth_buffer_.read(
        [&](uint16_t type, uint16_t count, void* body, uint32_t /*len*/) {
          auto* meta = static_cast<DepthMeta*>(body);
          auto* entries = reinterpret_cast<MarketDataEntry*>(
              static_cast<uint8_t*>(body) + sizeof(DepthMeta));
          handler(type, *meta, entries, count);
        });
  }

  /**
   * Snapshot 읽기
   * @param handler void(const DepthMeta& meta,
   *                     const MarketDataEntry* entries, size_t count)
   * @return 읽은 메시지 수
   */
  template <typename Handler>
  size_t read_snapshot(Handler&& handler) noexcept {
    return snapshot_buffer_.read(
        [&](uint16_t /*type*/, uint16_t count, void* body, uint32_t /*len*/) {
          auto* meta = static_cast<DepthMeta*>(body);
          auto* entries = reinterpret_cast<MarketDataEntry*>(
              static_cast<uint8_t*>(body) + sizeof(DepthMeta));
          handler(*meta, entries, count);
        });
  }

  /**
   * 모든 버퍼에서 읽기 (단일 핸들러)
   * @param handler void(uint16_t type, const DepthMeta& meta,
   *                     const MarketDataEntry* entries, size_t count)
   * @return 총 읽은 메시지 수
   */
  template <typename Handler>
  size_t read_all(Handler&& handler) noexcept {
    size_t total = 0;

    // Trade 읽기
    total += trade_buffer_.read(
        [&](uint16_t type, uint16_t /*count*/, void* body, uint32_t /*len*/) {
          auto* entry = static_cast<MarketDataEntry*>(body);
          const DepthMeta meta{0, 0, 0};
          handler(type, meta, entry, 1);
        });

    // Depth 읽기
    total += depth_buffer_.read(
        [&](uint16_t type, uint16_t count, void* body, uint32_t /*len*/) {
          auto* meta = static_cast<DepthMeta*>(body);
          auto* entries = reinterpret_cast<MarketDataEntry*>(
              static_cast<uint8_t*>(body) + sizeof(DepthMeta));
          handler(type, *meta, entries, count);
        });

    // Snapshot 읽기
    total += snapshot_buffer_.read(
        [&](uint16_t type, uint16_t count, void* body, uint32_t /*len*/) {
          auto* meta = static_cast<DepthMeta*>(body);
          auto* entries = reinterpret_cast<MarketDataEntry*>(
              static_cast<uint8_t*>(body) + sizeof(DepthMeta));
          handler(type, *meta, entries, count);
        });

    return total;
  }

  // ============== Status API ==============

  [[nodiscard]] bool trade_empty() const noexcept {
    return trade_buffer_.empty();
  }

  [[nodiscard]] bool depth_empty() const noexcept {
    return depth_buffer_.empty();
  }

  [[nodiscard]] bool snapshot_empty() const noexcept {
    return snapshot_buffer_.empty();
  }

  [[nodiscard]] bool empty() const noexcept {
    return trade_empty() && depth_empty() && snapshot_empty();
  }

 private:
  VarLengthRingBuffer trade_buffer_;
  VarLengthRingBuffer depth_buffer_;
  VarLengthRingBuffer snapshot_buffer_;
};

}  // namespace common

#endif  // MARKET_DATA_RING_BUFFER_HPP
