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

#include "ws_order_manager.h"

#include <charconv>

#include "common/types.h"
#include "response_manager.h"
#include "schema/response/execution_report.h"

namespace core {

void WsOrderManager::register_pending_request(
    const PendingOrderRequest& request) {
  logger_.debug(std::format(
      "[WsOrderManager] Registered pending request: id={}, symbol={}, side={}",
      request.client_order_id,
      request.symbol,
      common::toString(request.side)));

  pending_requests_[request.client_order_id] = request;
}

void WsOrderManager::remove_pending_request(std::uint64_t request_id) {
  const auto pending_request = pending_requests_.find(request_id);
  if (pending_request != pending_requests_.end()) {
    logger_.debug(std::format("[WsOrderManager] Removed pending request: id={}",
        request_id));
    pending_requests_.erase(pending_request);
  }
}

std::optional<schema::ExecutionReportResponse>
WsOrderManager::create_synthetic_execution_report(std::string_view request_id,
    int error_code, std::string_view error_message) {
  const auto client_order_id_opt = extract_client_order_id(request_id);
  if (!client_order_id_opt.has_value()) {
    logger_.error(std::format(
        "[WsOrderManager] Failed to extract clientOrderId from request_id: {}",
        request_id));
    return std::nullopt;
  }

  uint64_t client_order_id = client_order_id_opt.value();

  const auto reqeust = pending_requests_.find(client_order_id);
  if (reqeust == pending_requests_.end()) {
    logger_.warn(
        std::format("[WsOrderManager] No pending request found for "
                    "clientOrderId={}, creating "
                    "minimal ExecutionReport",
            client_order_id));
  }

  schema::ExecutionReportResponse response;
  response.subscription_id = 1;

  auto& event = response.event;
  event.event_type = "executionReport";
  event.event_time = 0;
  event.client_order_id = client_order_id;
  event.execution_type = "REJECTED";
  event.order_status = "REJECTED";
  event.reject_reason = error_message.data();

  event.order_price = 0.;
  event.order_quantity = 0.;
  event.last_executed_quantity = 0.;
  event.cumulative_filled_quantity = 0.;
  event.last_executed_price = 0.;

  // Fill in details from pending request if available
  if (LIKELY(reqeust != pending_requests_.end())) {
    const auto& pending = reqeust->second;
    event.symbol = pending.symbol;
    event.side = common::toString(pending.side);
    event.order_type = trading::toString(pending.ord_type);
    event.time_in_force = trading::toString(pending.time_in_force);
    event.order_price = pending.price.value;
    event.order_quantity = pending.order_qty.value;
  } else {
    event.symbol = "";
    event.side = "UNKNOWN";
    event.order_type = "UNKNOWN";
    event.time_in_force = "UNKNOWN";
    event.order_price = 0.0;
    event.order_quantity = 0.0;
  }

  logger_.info(
      std::format("[WsOrderManager] Created synthetic ExecutionReport: "
                  "clientOrderId={}, error_code={}, error={}",
          client_order_id,
          error_code,
          error_message));

  if (LIKELY(reqeust != pending_requests_.end())) {
    pending_requests_.erase(reqeust);
  }

  return response;
}

std::optional<uint64_t> WsOrderManager::extract_client_order_id(
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

  const std::string_view numeric_part = request_id.substr(last_underscore + 1);
  if (numeric_part.empty()) {
    return std::nullopt;
  }

  uint64_t result = 0;
  constexpr int kNumericNanoSecondsSize = 20;
  auto [ptr, ec] = std::from_chars(numeric_part.data(),
      numeric_part.data() + kNumericNanoSecondsSize,
      result);

  if (ec == std::errc() && ptr == numeric_part.data() + numeric_part.size()) {
    return result;
  }

  return std::nullopt;
}

}  // namespace core
