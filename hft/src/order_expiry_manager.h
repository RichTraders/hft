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

#ifndef ORDER_EXPIRY_MANAGER_H
#define ORDER_EXPIRY_MANAGER_H

#include <cstdint>
#include <optional>
#include <ostream>
#include <queue>
#include <vector>

#include "orders.h"
#include "types.h"

namespace trading {

class OrderExpiryManager {
 public:
  struct ExpiryKey {
    uint64_t expire_ts{0};
    common::TickerId symbol;
    common::Side side{};
    std::optional<common::PositionSide> position_side;
    uint32_t layer{0};
    common::OrderId cl_order_id;

    auto operator<=>(const ExpiryKey& key) const noexcept {
      return expire_ts <=> key.expire_ts;
    }

    friend std::ostream& operator<<(std::ostream& stream,
        const ExpiryKey& key) {
      stream << "expire_ts: " << key.expire_ts << ", symbol: " << key.symbol
             << ", side: " << common::toString(key.side) << ", position_side: "
             << (key.position_side ? common::toString(*key.position_side)
                                   : "none")
             << ", layer: " << key.layer
             << ", cl_order_id: " << key.cl_order_id.value;
      return stream;
    }
  };

  OrderExpiryManager(uint64_t ttl_reserved_ns, uint64_t ttl_live_ns)
      : ttl_reserved_ns_(ttl_reserved_ns), ttl_live_ns_(ttl_live_ns) {}

  ~OrderExpiryManager() = default;

  void register_expiry(const common::TickerId& ticker, common::Side side,
      std::optional<common::PositionSide> position_side, uint32_t layer,
      const common::OrderId& order_id, OMOrderState state,
      uint64_t now_ns) noexcept {
    const auto ttl = (state == OMOrderState::kReserved ||
                         state == OMOrderState::kCancelReserved)
                         ? ttl_reserved_ns_
                         : ttl_live_ns_;
    expiry_pq_.push(ExpiryKey{.expire_ts = now_ns + ttl,
        .symbol = ticker,
        .side = side,
        .position_side = position_side,
        .layer = layer,
        .cl_order_id = order_id});
  }

  std::vector<ExpiryKey> sweep_expired(uint64_t now_ns) noexcept {
    std::vector<ExpiryKey> expired;

    while (!expiry_pq_.empty() && expiry_pq_.top().expire_ts <= now_ns) {
      expired.push_back(expiry_pq_.top());
      expiry_pq_.pop();
    }

    return expired;
  }

  void configure_ttl(uint64_t ttl_reserved_ns, uint64_t ttl_live_ns) noexcept {
    ttl_reserved_ns_ = ttl_reserved_ns;
    ttl_live_ns_ = ttl_live_ns;
  }

  [[nodiscard]] size_t pending_count() const noexcept {
    return expiry_pq_.size();
  }

 private:
  using MinHeap =
      std::priority_queue<ExpiryKey, std::vector<ExpiryKey>, std::greater<>>;

  MinHeap expiry_pq_;
  uint64_t ttl_reserved_ns_;
  uint64_t ttl_live_ns_;
};

}  // namespace trading

#endif  // ORDER_EXPIRY_MANAGER_H
