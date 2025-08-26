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

#ifndef TYPES_H
#define TYPES_H

namespace common {
struct OrderId {
  uint64_t value{std::numeric_limits<uint64_t>::max()};
  explicit OrderId() = default;
  explicit OrderId(uint64_t data) : value(data) {}

  [[nodiscard]] bool is_valid() const {
    return value != std::numeric_limits<uint64_t>::max();
  }

  constexpr bool operator==(uint64_t other) const { return value == other; }
  constexpr bool operator==(const OrderId& other) const {
    return value == other.value;
  }

  constexpr OrderId& operator++(int) {
    value++;
    return (*this);
  }

  template <typename H>
  friend H AbslHashValue(H hash_state, const OrderId& oid) {
    return H::combine(std::move(hash_state), oid.value);
  }

  explicit operator uint64_t() const { return value; }
};

inline auto toString(OrderId order_id) -> std::string {
  return UNLIKELY(!order_id.is_valid()) ? "INVALID"
                                        : std::to_string(order_id.value);
}

constexpr auto kOrderIdInvalid = std::numeric_limits<uint64_t>::max();

using TickerId = std::string;
constexpr auto kTickerIdInvalid = "";

struct ClientId {
  uint32_t value{std::numeric_limits<uint32_t>::max()};
  explicit ClientId() = default;
  explicit ClientId(uint32_t data) : value(data) {}

  [[nodiscard]] bool is_valid() const {
    return value != std::numeric_limits<uint32_t>::max();
  }

  constexpr bool operator==(uint32_t other) const { return value == other; }
  explicit operator uint32_t() const { return value; }
};

inline auto toString(ClientId client_id) -> std::string {
  return UNLIKELY(!client_id.is_valid())
             ? "INVALID"
             : std::to_string(static_cast<uint32_t>(client_id));
}

constexpr auto kClientIdInvalid = std::numeric_limits<uint32_t>::max();

struct Price {
  double value{std::numeric_limits<double>::max()};

  Price() = default;
  explicit Price(double data) : value(data) {}

  constexpr bool operator==(double other) const { return value == other; }

  constexpr bool operator!=(double other) const { return value != other; }
  constexpr bool operator<(const Price& other) const {
    return value < other.value;
  }
  constexpr bool operator==(const Price& other) const {
    return value == other.value;
  }

  [[nodiscard]] constexpr bool isValid() const {
    return value != std::numeric_limits<double>::max();
  }
  friend constexpr bool operator==(double lhs, const Price& rhs) {
    return rhs == lhs;
  }
  friend constexpr bool operator!=(double lhs, const Price& rhs) {
    return rhs != lhs;
  }
};

constexpr auto kPriceInvalid = std::numeric_limits<double>::max();

inline auto toString(Price price) -> std::string {
  return price.isValid() ? std::to_string(price.value) : "INVALID";
}

struct Qty {
  double value{std::numeric_limits<double>::max()};

  Qty() = default;
  constexpr explicit Qty(double data) : value(data) {}

  [[nodiscard]] bool is_valid() const {
    return value != std::numeric_limits<double>::max();
  }

  constexpr bool operator==(double other) const { return value == other; }
  constexpr bool operator<(const Qty& other) const {
    return value < other.value;
  }
  constexpr bool operator==(const Qty& other) const {
    return value == other.value;
  }

  constexpr Qty& operator+=(const Qty& other) {
    value += other.value;
    return *this;
  }
  constexpr Qty& operator+=(double other) {
    value += other;
    return *this;
  }
  constexpr Qty& operator-=(const Qty& other) {
    value -= other.value;
    return *this;
  }
  constexpr Qty& operator-=(double other) {
    value -= other;
    return *this;
  }
  constexpr Qty& operator*=(double other) {
    value *= other;
    return *this;
  }
  constexpr Qty operator-() const noexcept { return Qty{-value}; }

  explicit operator double() const { return value; }

  friend constexpr Qty operator+(Qty lhs, const Qty& rhs) noexcept {
    lhs += rhs;
    return lhs;
  }
  friend constexpr Qty operator+(Qty lhs, double rhs) noexcept {
    lhs += rhs;
    return lhs;
  }
  friend constexpr Qty operator+(double lhs, Qty rhs) noexcept {
    rhs += lhs;
    return rhs;
  }

  friend constexpr Qty operator-(Qty lhs, const Qty& rhs) noexcept {
    lhs -= rhs;
    return lhs;
  }
  friend constexpr Qty operator-(Qty lhs, double rhs) noexcept {
    lhs -= rhs;
    return lhs;
  }
  friend constexpr Qty operator-(double lhs, const Qty& rhs) noexcept {
    return Qty{lhs - rhs.value};
  }

  friend constexpr Qty operator*(Qty lhs, double rhs) noexcept {
    lhs *= rhs;
    return lhs;
  }
  friend constexpr Qty operator*(double lhs, Qty rhs) noexcept {
    rhs *= lhs;
    return rhs;
  }
};

inline auto toString(Qty qty) -> std::string {
  return UNLIKELY(!qty.is_valid()) ? "INVALID" : std::to_string(qty.value);
}

constexpr auto kQtyInvalid = std::numeric_limits<double>::max();

struct Priority {
  uint64_t value{std::numeric_limits<uint64_t>::max()};

  explicit Priority(uint64_t data) : value(data) {}

  [[nodiscard]] bool is_valid() const {
    return value != std::numeric_limits<uint64_t>::max();
  }

  constexpr bool operator==(uint64_t other) const { return value == other; }
  explicit operator uint64_t() const { return value; }
};

inline auto toString(Priority priority) -> std::string {
  return UNLIKELY(!priority.is_valid())
             ? "INVALID"
             : std::to_string(static_cast<uint64_t>(priority));
}

constexpr auto kPriorityInvalid = std::numeric_limits<uint64_t>::max();

enum class Side : int8_t {
  kBuy = 0,
  kSell = 1,
  kTrade = 2,
  kInvalid = 3,
};

inline auto toString(const Side side) -> std::string {
  switch (side) {
    case Side::kBuy:
      return "BUY";
    case Side::kSell:
      return "SELL";
    case Side::kTrade:
      return "TRADE";
    case Side::kInvalid:
      return "INVALID";
  }

  return "UNKNOWN";
}

inline Side charToSide(const char character) {
  switch (character) {
    case '0':
      return Side::kBuy;
    case '1':
      return Side::kSell;
    case '2':
      return Side::kTrade;
    default:
      return Side::kInvalid;
  }
}

constexpr auto sideToIndex(Side side) noexcept {
  return static_cast<size_t>(side);
}

constexpr auto oppIndex(size_t idx) noexcept -> size_t {
  return idx ^ 1U;
}

constexpr auto sideToValue(Side side) noexcept {
  const int ret = side == Side::kBuy ? 1 : -1;
  return ret;
}

enum class MarketUpdateType : uint8_t {
  kInvalid = 0,
  kClear = 1,
  kAdd = 2,
  kModify = 3,
  kCancel = 4,
  kTrade = 5,
};

inline MarketUpdateType charToMarketUpdateType(const char character) {
  switch (character) {
    case '0':
      return MarketUpdateType::kAdd;
    case '1':
      return MarketUpdateType::kModify;
    case '2':
      return MarketUpdateType::kCancel;
    default:
      return MarketUpdateType::kInvalid;
  }
}

inline std::string toString(MarketUpdateType type) {
  switch (type) {
    case MarketUpdateType::kClear:
      return "CLEAR";
    case MarketUpdateType::kAdd:
      return "ADD";
    case MarketUpdateType::kModify:
      return "MODIFY";
    case MarketUpdateType::kCancel:
      return "CANCEL";
    case MarketUpdateType::kTrade:
      return "TRADE";
    case MarketUpdateType::kInvalid:
      return "INVALID";
  }
  return "UNKNOWN";
}

struct RiskCfg {
  Qty max_order_size_ = Qty{0};
  Qty max_position_ = Qty{0};
  Qty min_position_ = Qty{0};
  double max_loss_ = 0;

  [[nodiscard]] auto toString() const {
    std::ostringstream stream;

    stream << "RiskCfg{"
           << "max-order-size:" << common::toString(max_order_size_) << " "
           << "max-position:" << common::toString(max_position_) << " "
           << "min-position:" << common::toString(min_position_) << " "
           << "max-loss:" << max_loss_ << "}";

    return stream.str();
  }
};

struct TradeEngineCfg {
  Qty clip_ = Qty{0};
  double threshold_ = 0;
  RiskCfg risk_cfg_;

  [[nodiscard]] auto toString() const {
    std::ostringstream stream;
    stream << "TradeEngineCfg{" << "clip:" << common::toString(clip_) << " "
           << "thresh:" << threshold_ << " " << "risk:" << risk_cfg_.toString()
           << "}";

    return stream.str();
  }
};

using TradeEngineCfgHashMap = std::unordered_map<std::string, TradeEngineCfg>;
}  // namespace common
#endif  //TYPES_H