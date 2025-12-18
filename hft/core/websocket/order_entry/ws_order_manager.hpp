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

#ifndef WS_ORDER_MANAGER_H
#define WS_ORDER_MANAGER_H

#include <tbb/concurrent_hash_map.h>

#include "common/logger.h"
#include "core/order_entry.h"
#include "schema/spot/response/execution_report.h"
#include "schema/spot/response/order.h"

namespace core {

// Pending request information for creating synthetic ExecutionReport
struct PendingOrderRequest {
  std::uint64_t client_order_id{0};
  std::string symbol;
  common::Side side{common::Side::kInvalid};
  common::Price price{0.0};
  common::Qty order_qty{0.0};
  trading::OrderType ord_type{trading::OrderType::kInvalid};
  trading::TimeInForce time_in_force{trading::TimeInForce::kInvalid};
  std::optional<common::PositionSide> position_side;
};

template <typename ExchangeTraits>
class WsOrderManager {
 public:
  explicit WsOrderManager(const common::Logger::Producer& logger)
      : logger_(logger) {}

  void register_pending_request(const PendingOrderRequest& request) {
    logger_.debug(
        "[WsOrderManager] Registered pending request: id={}, symbol={}, "
        "side={}",
        request.client_order_id,
        request.symbol,
        common::toString(request.side));

    typename PendingRequestMap::accessor accessor;
    pending_requests_.insert(accessor, request.client_order_id);
    accessor->second = request;
  }

  void remove_pending_request(std::uint64_t request_id) {
    typename PendingRequestMap::accessor accessor;
    if (pending_requests_.find(accessor, request_id)) {
      logger_.debug("[WsOrderManager] Removed pending request: id={}",
          request_id);
      pending_requests_.erase(accessor);
    }
  }

  std::optional<typename ExchangeTraits::ExecutionReportResponse>
  create_synthetic_execution_report(std::string_view request_id, int error_code,
      std::string_view error_message) {
    const auto client_order_id_opt = extract_client_order_id(request_id);
    if (UNLIKELY(!client_order_id_opt.has_value())) {
      logger_.error(
          "[WsOrderManager] Failed to extract clientOrderId from request_id: "
          "{}",
          request_id);
      return std::nullopt;
    }

    uint64_t client_order_id = client_order_id_opt.value();

    typename PendingRequestMap::accessor accessor;
    bool found = pending_requests_.find(accessor, client_order_id);

    if (!found) {
      logger_.warn(
          "[WsOrderManager] No pending request found for "
          "clientOrderId={}, creating "
          "minimal ExecutionReport",
          client_order_id);
    }

    typename ExchangeTraits::ExecutionReportResponse response;

    // Initialize response wrapper based on exchange type
    if constexpr (ExchangeTraits::requires_listen_key()) {
      // Futures: Set event_type, event_time, transaction_time
      response.event_type = "ORDER_TRADE_UPDATE";
      response.event_time = 0;
      response.transaction_time = 0;
    } else {
      // Spot: Set subscription_id
      response.subscription_id = 0;
    }

    auto& event = response.event;
    event.client_order_id = client_order_id;
    event.execution_type = "REJECTED";
    event.order_status = "REJECTED";
    event.reject_reason = error_message.data();
    event.order_price = 0.;
    event.order_quantity = 0.;

    if (found) {
      const auto& pending = accessor->second;
      event.symbol = pending.symbol;
      event.side = common::toString(pending.side);
      event.order_type = trading::toString(pending.ord_type);
      event.time_in_force = trading::toString(pending.time_in_force);
      event.order_price = pending.price.value;
      event.order_quantity = pending.order_qty.value;

      if constexpr (ExchangeTraits::requires_listen_key()) {
        if (pending.position_side) {
          event.position_side = common::toString(*pending.position_side);
        } else {
          event.position_side = "BOTH";  // Default for futures
        }
      }
    } else {
      event.symbol = INI_CONFIG.get("meta", "ticker");
      event.side = "UNKNOWN";
      event.order_type = "UNKNOWN";
      event.time_in_force = "UNKNOWN";
      event.order_price = 0.0;
      event.order_quantity = 0.0;

      if constexpr (ExchangeTraits::requires_listen_key()) {
        event.position_side = "BOTH";
      }
    }

    logger_.info(
        "[WsOrderManager] Created synthetic ExecutionReport: "
        "clientOrderId={}, error_code={}, error={}",
        client_order_id,
        error_code,
        error_message);

    if (found) {
      pending_requests_.erase(accessor);
    }

    return response;
  }

  // Cancel-and-reorder pair tracking
  void register_cancel_and_reorder_pair(std::uint64_t new_order_id,
      std::uint64_t original_order_id) {
    typename CancelReorderMap::accessor accessor;
    cancel_reorder_pairs_.insert(accessor, new_order_id);
    accessor->second = original_order_id;
    logger_.debug(
        "[WsOrderManager] Registered cancel_and_reorder pair: "
        "new_order_id={}, original_order_id={}",
        new_order_id,
        original_order_id);
  }

  [[nodiscard]] std::optional<std::uint64_t> get_original_order_id(
      std::uint64_t new_order_id) const {
    typename CancelReorderMap::const_accessor accessor;
    if (cancel_reorder_pairs_.find(accessor, new_order_id)) {
      return accessor->second;
    }
    return std::nullopt;
  }

  void remove_cancel_and_reorder_pair(std::uint64_t new_order_id) {
    typename CancelReorderMap::accessor accessor;
    if (cancel_reorder_pairs_.find(accessor, new_order_id)) {
      logger_.debug(
          "[WsOrderManager] Removed cancel_and_reorder pair: new_order_id={}",
          new_order_id);
      cancel_reorder_pairs_.erase(accessor);
    }
  }

  static std::optional<uint64_t> extract_client_order_id(
      std::string_view request_id) {
    // Request ID patterns:
    // clientOrderId is epoch time in nano seconds
    // "orderplace_{clientOrderId}"
    // "ordercancel_{clientOrderId}"
    // "orderreplace_{clientOrderId}"
    // "ordercancelAll_{clientOrderId}"

    const size_t last_underscore = request_id.find('_');
    if (last_underscore == std::string_view::npos) {
      return std::nullopt;
    }

    const std::string_view numeric_part =
        request_id.substr(last_underscore + 1);
    if (numeric_part.empty()) {
      return std::nullopt;
    }

    uint64_t result = 0;
    auto [ptr, ec] = std::from_chars(numeric_part.data(),
        numeric_part.data() + numeric_part.size(),
        result);

    if (ec == std::errc() && ptr == numeric_part.data() + numeric_part.size()) {
      return result;
    }

    return std::nullopt;
  }

 private:
  const common::Logger::Producer& logger_;

  // Cancel-and-reorder pair tracking
  // Hash comparator for TBB concurrent_hash_map
  struct Uint64HashCompare {
    static size_t hash(const std::uint64_t& key) {
      return std::hash<std::uint64_t>{}(key);
    }
    static bool equal(const std::uint64_t& lhs, const std::uint64_t& rhs) {
      return lhs == rhs;
    }
  };

  using PendingRequestMap = tbb::concurrent_hash_map<std::uint64_t,
      PendingOrderRequest, Uint64HashCompare>;
  using CancelReorderMap =
      tbb::concurrent_hash_map<std::uint64_t, std::uint64_t, Uint64HashCompare>;

  PendingRequestMap pending_requests_;
  CancelReorderMap cancel_reorder_pairs_;
};

}  // namespace core

#endif  //WS_ORDER_MANAGER_H
