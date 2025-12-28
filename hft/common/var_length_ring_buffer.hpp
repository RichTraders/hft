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

#ifndef VAR_LENGTH_RING_BUFFER_HPP
#define VAR_LENGTH_RING_BUFFER_HPP

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

namespace common {

constexpr size_t kCacheLineSize = 64;
constexpr uint32_t kAlignment = 8;
constexpr uint32_t kAlignMask = kAlignment - 1;

// 8바이트 정렬 헬퍼
constexpr uint32_t align_up_8(uint32_t size) noexcept {
  return (size + kAlignMask) & ~kAlignMask;
}

// 메시지 헤더 (8 bytes)
struct alignas(kAlignment) RingBufferMsgHeader {
  uint32_t length;  // 헤더 포함 전체 길이
  uint16_t type;    // 메시지 타입
  uint16_t count;   // entries 개수 (optional)
};

static_assert(sizeof(RingBufferMsgHeader) == kAlignment,
    "Header must be 8 bytes");

// 메시지 타입
enum class RingBufferMsgType : uint16_t {
  kPadding = 0xFFFF,
  kTrade = 1,
  kDepth = 2,
  kBookTicker = 3,
  kSnapshot = 4,
};

/**
 * Single-Producer Single-Consumer Variable-Length Ring Buffer
 *
 * 특징:
 * - Zero allocation: 버퍼 내 직접 쓰기
 * - Lock-free: atomic load/store만 사용
 * - Cache-friendly: Producer/Consumer 캐시라인 분리
 *
 * 메모리 레이아웃:
 * [Header1|Data1...][Header2|Data2...][Padding][Header3|Data3...]
 *                                     ^wrap
 */
class VarLengthRingBuffer {
 public:
  explicit VarLengthRingBuffer(size_t capacity)
      : capacity_(capacity),
        buffer_(new uint8_t[capacity]),  // NOLINT(modernize-avoid-c-arrays)
        write_pos_(0),
        read_pos_(0) {}

  // Non-copyable, non-movable
  VarLengthRingBuffer(const VarLengthRingBuffer&) = delete;
  VarLengthRingBuffer& operator=(const VarLengthRingBuffer&) = delete;
  VarLengthRingBuffer(VarLengthRingBuffer&&) = delete;
  VarLengthRingBuffer& operator=(VarLengthRingBuffer&&) = delete;

  /**
   * [Producer] 쓰기 공간 예약
   * @param total_len 헤더 포함 전체 길이 (8바이트 정렬 필요)
   * @return 쓰기 시작 포인터 (실패 시 nullptr)
   */
  [[nodiscard]] void* begin_write(uint32_t total_len) noexcept {
    total_len = align_up_8(total_len);

    uint64_t curr_write = write_pos_.load(std::memory_order_relaxed);
    const uint64_t curr_read = read_pos_.load(std::memory_order_acquire);

    // curr_write가 capacity에 도달했으면 0으로 리셋
    if (curr_write >= capacity_) {
      curr_write = 0;
    }

    // Case 1: 버퍼 끝에 공간 부족 → wrap-around
    if (curr_write + total_len > capacity_) {
      const auto remaining = static_cast<uint32_t>(capacity_ - curr_write);

      // Padding 쓸 공간도 없으면 0으로 가야 함
      if (remaining >= sizeof(RingBufferMsgHeader)) {
        // Padding 헤더 작성
        auto* pad_header =
            reinterpret_cast<RingBufferMsgHeader*>(buffer_.get() + curr_write);
        pad_header->length = remaining;
        pad_header->type = static_cast<uint16_t>(RingBufferMsgType::kPadding);
        pad_header->count = 0;
      }

      // 0번지에 공간 있는지 확인
      // curr_read가 0~total_len 사이에 있으면 덮어쓰게 됨
      if (curr_read > 0 && curr_read <= total_len) {
        return nullptr;  // 공간 부족
      }
      // curr_read가 0이고 curr_write도 0이면 버퍼 비어있음
      if (curr_read == 0 && curr_write == 0) {
        // OK, 버퍼 비어있음
      } else if (curr_read == 0) {
        return nullptr;  // Consumer가 0번지를 아직 안 지나감
      }

      // 0번지부터 시작
      pending_write_pos_ = 0;
      pending_write_len_ = total_len;
      pending_wrapped_ = true;
      return buffer_.get();
    }

    // Case 2: 일반적인 쓰기
    // Read가 Write 뒤에 있고, 쓰려는 범위와 겹치면 실패
    if (curr_read > curr_write && curr_read <= curr_write + total_len) {
      return nullptr;  // 공간 부족
    }

    pending_write_pos_ = curr_write;
    pending_write_len_ = total_len;
    pending_wrapped_ = false;
    return buffer_.get() + curr_write;
  }

  /**
   * [Producer] 쓰기 커밋
   * begin_write() 성공 후 데이터 작성 완료 시 호출
   */
  void commit_write() noexcept {
    const uint64_t new_pos = pending_write_pos_ + pending_write_len_;
    write_pos_.store(new_pos, std::memory_order_release);
  }

  /**
   * [Producer] 쓰기 (헬퍼 함수)
   * 헤더 + 데이터를 한 번에 쓰기
   */
  template <typename T>
  [[nodiscard]] bool write(uint16_t type, const T& data,
      uint16_t count = 0) noexcept {
    const uint32_t body_len = sizeof(T);
    const uint32_t total_len =
        align_up_8(sizeof(RingBufferMsgHeader) + body_len);

    void* ptr = begin_write(total_len);
    if (!ptr) {
      return false;
    }

    auto* header = static_cast<RingBufferMsgHeader*>(ptr);
    header->length = total_len;
    header->type = type;
    header->count = count;

    std::memcpy(reinterpret_cast<uint8_t*>(ptr) + sizeof(RingBufferMsgHeader),
        &data,
        body_len);

    commit_write();
    return true;
  }

  /**
   * [Producer] 가변 길이 쓰기
   * 헤더 + 메타데이터 + entries 배열
   */
  template <typename Meta, typename Entry>
  [[nodiscard]] bool write_var(uint16_t type, const Meta& meta,
      const Entry* entries, size_t entry_count) noexcept {
    const uint32_t body_len = sizeof(Meta) + entry_count * sizeof(Entry);
    const uint32_t total_len =
        align_up_8(sizeof(RingBufferMsgHeader) + body_len);

    void* ptr = begin_write(total_len);
    if (!ptr) {
      return false;
    }

    auto* header = static_cast<RingBufferMsgHeader*>(ptr);
    header->length = total_len;
    header->type = type;
    header->count = static_cast<uint16_t>(entry_count);

    uint8_t* body =
        reinterpret_cast<uint8_t*>(ptr) + sizeof(RingBufferMsgHeader);
    std::memcpy(body, &meta, sizeof(Meta));
    std::memcpy(body + sizeof(Meta), entries, entry_count * sizeof(Entry));

    commit_write();
    return true;
  }

  /**
   * [Consumer] 메시지 읽기
   * @param handler 콜백: void(uint16_t type, void* body, uint32_t body_len)
   * @return 읽은 메시지 수
   */
  template <typename Handler>
  size_t read(Handler&& handler) noexcept {
    size_t count = 0;
    uint64_t curr_read = read_pos_.load(std::memory_order_relaxed);
    uint64_t curr_write = write_pos_.load(std::memory_order_acquire);

    while (curr_read != curr_write) {
      // capacity에 도달하면 0으로 wrap
      if (curr_read >= capacity_) {
        curr_read = 0;
        read_pos_.store(0, std::memory_order_release);
        curr_write = write_pos_.load(std::memory_order_acquire);
        if (curr_read == curr_write) {
          break;
        }
      }

      auto* header =
          reinterpret_cast<RingBufferMsgHeader*>(buffer_.get() + curr_read);

      // Padding이면 건너뛰기
      if (header->type == static_cast<uint16_t>(RingBufferMsgType::kPadding)) {
        curr_read = 0;
        read_pos_.store(0, std::memory_order_release);
        curr_write = write_pos_.load(std::memory_order_acquire);
        continue;
      }

      // 실제 데이터 처리
      void* body = buffer_.get() + curr_read + sizeof(RingBufferMsgHeader);
      uint32_t body_len = header->length - sizeof(RingBufferMsgHeader);

      handler(header->type, header->count, body, body_len);

      curr_read += header->length;
      read_pos_.store(curr_read, std::memory_order_release);
      ++count;
    }

    return count;
  }

  /**
   * [Consumer] 단일 메시지 읽기 (non-blocking)
   * @return 읽은 메시지가 있으면 true
   */
  template <typename Handler>
  bool read_one(Handler&& handler) noexcept {
    uint64_t curr_read = read_pos_.load(std::memory_order_relaxed);
    uint64_t curr_write = write_pos_.load(std::memory_order_acquire);

    if (curr_read == curr_write) {
      return false;
    }

    // capacity에 도달하면 0으로 wrap
    if (curr_read >= capacity_) {
      curr_read = 0;
      read_pos_.store(0, std::memory_order_release);
      curr_write = write_pos_.load(std::memory_order_acquire);
      if (curr_read == curr_write) {
        return false;
      }
    }

    auto* header =
        reinterpret_cast<RingBufferMsgHeader*>(buffer_.get() + curr_read);

    // Padding이면 건너뛰고 다시 시도
    if (header->type == static_cast<uint16_t>(RingBufferMsgType::kPadding)) {
      read_pos_.store(0, std::memory_order_release);
      return read_one(std::forward<Handler>(handler));
    }

    void* body = buffer_.get() + curr_read + sizeof(RingBufferMsgHeader);
    uint32_t body_len = header->length - sizeof(RingBufferMsgHeader);

    handler(header->type, header->count, body, body_len);

    read_pos_.store(curr_read + header->length, std::memory_order_release);
    return true;
  }

  [[nodiscard]] bool empty() const noexcept {
    return read_pos_.load(std::memory_order_acquire) ==
           write_pos_.load(std::memory_order_acquire);
  }

  [[nodiscard]] size_t capacity() const noexcept { return capacity_; }

 private:
  const size_t capacity_;
  std::unique_ptr<uint8_t[]> buffer_;  // NOLINT(modernize-avoid-c-arrays)

  // Producer 전용 (캐시라인 분리)
  alignas(kCacheLineSize) std::atomic<uint64_t> write_pos_;
  uint64_t pending_write_pos_ = 0;
  uint32_t pending_write_len_ = 0;
  bool pending_wrapped_ = false;

  // Consumer 전용 (캐시라인 분리)
  alignas(kCacheLineSize) std::atomic<uint64_t> read_pos_;
};

}  // namespace common

#endif  // VAR_LENGTH_RING_BUFFER_HPP
