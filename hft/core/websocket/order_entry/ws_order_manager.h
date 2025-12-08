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

#include "absl/container/flat_hash_map.h"
#include "common/logger.h"
#include "core/order_entry.h"
#include "schema/response/execution_report.h"
#include "schema/response/order.h"

namespace trading {
class ResponseManager;
}

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
};

class WsOrderManager {
 public:
  explicit WsOrderManager(const common::Logger::Producer& logger)
      : logger_(logger) {}

  void register_pending_request(const PendingOrderRequest& request);

  void remove_pending_request(std::uint64_t request_id);

  std::optional<schema::ExecutionReportResponse>
  create_synthetic_execution_report(std::string_view request_id, int error_code,
      std::string_view error_message);

  // Cancel-and-reorder pair tracking
  void register_cancel_and_reorder_pair(std::uint64_t new_order_id,
      std::uint64_t original_order_id);
  [[nodiscard]] std::optional<std::uint64_t> get_original_order_id(
      std::uint64_t new_order_id) const;
  void remove_cancel_and_reorder_pair(std::uint64_t new_order_id);

  static std::optional<uint64_t> extract_client_order_id(
      std::string_view request_id);

 private:
  const common::Logger::Producer& logger_;

  // Cancel-and-reorder pair tracking
  absl::flat_hash_map<std::uint64_t, PendingOrderRequest> pending_requests_;
  absl::flat_hash_map<std::uint64_t, std::uint64_t> cancel_reorder_pairs_;
};

}  // namespace core

#endif  //WS_ORDER_MANAGER_H
