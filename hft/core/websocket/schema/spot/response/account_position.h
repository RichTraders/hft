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

#ifndef ACCOUNT_POSITION_H
#define ACCOUNT_POSITION_H

#include <glaze/glaze.hpp>

namespace schema {

struct AccountBalance {
  std::string asset;        // "a"
  double free_balance{};    // "f"
  double locked_balance{};  // "l"

  // clang-format off
  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = AccountBalance;
    static constexpr auto value = glz::object(  // NOLINT(readability-identifier-naming)
        "a", &T::asset,
        "f", ::glz::quoted_num<&T::free_balance>,
        "l", ::glz::quoted_num<&T::locked_balance>);
  };
  // clang-format on
};
inline std::ostream& operator<<(
    std::ostream& out, const AccountBalance& balance) {
  out << "asset: " << balance.asset << "\n";
  out << "free_balance: " << balance.free_balance << "\n";
  out << "locked_balance: " << balance.locked_balance << "\n";
  return out;
}

struct OutboundAccountPositionEvent {
  std::string event_type;                // "e"
  std::uint64_t event_time{};             // "E"
  std::uint64_t last_update_time{};       // "u"
  std::vector<AccountBalance> balances;  // "B"

  // clang-format off
  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = OutboundAccountPositionEvent;
    static constexpr auto value = glz::object(  // NOLINT(readability-identifier-naming)
        "e", &T::event_type,
        "E", &T::event_time,
        "u", &T::last_update_time,
        "B", &T::balances);
  };
  // clang-format on
};
inline std::ostream& operator<<(std::ostream& stream,
    const OutboundAccountPositionEvent& event) {
  stream << "event_type: " << event.event_type << "\n";
  stream << "event_time: " << event.event_time << "\n";
  stream << "last_update_time: " << event.last_update_time << "\n";
  for (const auto& balance : event.balances) {
    stream << "balances: " << balance;
  }

  return stream;
}


struct BalanceUpdateEvent {
  std::string event_type;     // "e"
  std::int64_t event_time{};  // "E"
  std::string asset;          // "a"
  double balance_delta{};     // "d"
  std::int64_t clear_time{};  // "T"

  // clang-format off
  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = BalanceUpdateEvent;
    static constexpr auto value = glz::object(  // NOLINT(readability-identifier-naming)
        "e", &T::event_type,
        "E", &T::event_time,
        "a", &T::asset,
        "d", ::glz::quoted_num<&T::balance_delta>,
        "T", &T::clear_time);
  };
  // clang-format on
};
inline std::ostream& operator<<(std::ostream& stream,
    const BalanceUpdateEvent& event) {
  stream << "event_type: " << event.event_type << "\n";
  stream << "event_time: " << event.event_time << "\n";
  stream << "asset: " << event.asset << "\n";
  stream << "balance_delta: " << event.balance_delta << "\n";
  stream << "clear_time: " << event.clear_time << "\n";
  return stream;
}

template <typename EventT>
struct SubscriptionEventEnvelope {
  std::int64_t subscription_id{};
  EventT event;

  // clang-format off
  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = SubscriptionEventEnvelope<EventT>;
    static constexpr auto value = glz::object(  // NOLINT(readability-identifier-naming)
        "subscriptionId", &T::subscription_id,
        "event", &T::event);
  };
  // clang-format on
};
template <typename T>
std::ostream& operator<<(std::ostream& stream,
    SubscriptionEventEnvelope<T>& event) {
  stream << "subscription_id:" << event.subscription_id
         << ", event:" << event.event;
  return stream;
}

using OutboundAccountPositionEnvelope =
    SubscriptionEventEnvelope<OutboundAccountPositionEvent>;

using BalanceUpdateEnvelope = SubscriptionEventEnvelope<BalanceUpdateEvent>;

}  // namespace schema
#endif  //ACCOUNT_POSITION_H
