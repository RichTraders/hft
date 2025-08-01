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

  explicit OrderId(uint64_t data) : value(data) {}

  [[nodiscard]] bool is_valid() const {
    return value != std::numeric_limits<uint64_t>::max();
  }

  bool operator==(uint64_t other) const { return value == other; }
  explicit operator uint64_t() const { return value; }
};

inline auto toString(OrderId order_id) -> std::string {
  return UNLIKELY(!order_id.is_valid())
             ? "INVALID"
             : std::to_string(static_cast<uint64_t>(order_id));
}

constexpr auto kOrderIdInvalid = std::numeric_limits<uint64_t>::max();

using TickerId = const std::string;
constexpr auto kTickerIdInvalid = "";

struct ClientId {
  uint32_t value{std::numeric_limits<uint32_t>::max()};

  explicit ClientId(uint32_t data) : value(data) {}

  [[nodiscard]] bool is_valid() const {
    return value != std::numeric_limits<uint32_t>::max();
  }

  bool operator==(uint32_t other) const { return value == other; }
  explicit operator uint32_t() const { return value; }
};

inline auto toString(ClientId client_id) -> std::string {
  return UNLIKELY(!client_id.is_valid())
             ? "INVALID"
             : std::to_string(static_cast<uint32_t>(client_id));
}

constexpr auto kClientIdInvalid = std::numeric_limits<uint32_t>::max();

struct Price {
  float value{std::numeric_limits<float>::max()};

  explicit Price(float data) : value(data) {}

  bool operator==(float other) const { return value == other; }

  bool operator!=(float other) const { return value != other; }
  bool operator<(const Price& other) const { return value < other.value; }
  bool operator==(const Price& other) const { return value == other.value; }

  [[nodiscard]] bool isValid() const {
    return value != std::numeric_limits<float>::max();
  }

  explicit operator float() const { return value; }
};

constexpr auto kPriceInvalid = std::numeric_limits<float>::max();

inline auto toString(Price price) -> std::string {
  return price.isValid() ? std::to_string(static_cast<float>(price))
                         : "INVALID";
}

struct Qty {
  float value{std::numeric_limits<float>::max()};

  explicit Qty(float data) : value(data) {}

  [[nodiscard]] bool is_valid() const {
    return value != std::numeric_limits<float>::max();
  }

  bool operator==(float other) const { return value == other; }
  bool operator<(const Qty& other) const { return value < other.value; }
  bool operator==(const Qty& other) const { return value == other.value; }

  Qty& operator+=(const Qty& other) {
    value += other.value;
    return *this;
  }

  Qty& operator+=(float other) {
    value += other;
    return *this;
  }

  Qty& operator-=(const Qty& other) {
    value -= other.value;
    return *this;
  }

  Qty& operator-=(float other) {
    value -= other;
    return *this;
  }

  explicit operator float() const { return value; }
};

inline auto toString(Qty qty) -> std::string {
  return UNLIKELY(!qty.is_valid()) ? "INVALID"
                                   : std::to_string(static_cast<float>(qty));
}

constexpr auto kQtyInvalid = std::numeric_limits<float>::max();

struct Priority {
  uint64_t value{std::numeric_limits<uint64_t>::max()};

  explicit Priority(uint64_t data) : value(data) {}

  [[nodiscard]] bool is_valid() const {
    return value != std::numeric_limits<uint64_t>::max();
  }

  bool operator==(uint64_t other) const { return value == other; }
  explicit operator uint64_t() const { return value; }
};

inline auto toString(Priority priority) -> std::string {
  return UNLIKELY(!priority.is_valid())
             ? "INVALID"
             : std::to_string(static_cast<uint64_t>(priority));
}

constexpr auto kPriorityInvalid = std::numeric_limits<uint64_t>::max();

enum class Side : int8_t {
  kInvalid = 0,
  kBuy = 1,
  kSell = -1,
  kTrade = 2,
  kMax = 3,
};

inline auto toString(const Side side) -> std::string {
  switch (side) {
    case Side::kBuy:
      return "BUY";
    case Side::kSell:
      return "SELL";
    case Side::kInvalid:
      return "INVALID";
    case Side::kTrade:
      return "TRADE";
    case Side::kMax:
      return "MAX";
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
  return static_cast<size_t>(side) + 1;
}

constexpr auto sideToValue(Side side) noexcept {
  return static_cast<int>(side);
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
  double max_loss_ = 0;

  [[nodiscard]] auto toString() const {
    std::ostringstream stream;

    stream << "RiskCfg{"
           << "max-order-size:" << common::toString(max_order_size_) << " "
           << "max-position:" << common::toString(max_position_) << " "
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