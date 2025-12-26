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

#ifndef BINANCE_FUTURES_OE_MAPPER_H
#define BINANCE_FUTURES_OE_MAPPER_H

#include "common/logger.h"
#include "core/response_manager.h"
#include "schema/futures/response/api_response.h"
#include "schema/futures/response/execution_report.h"

namespace core {

class BinanceFuturesOeMapper {
 public:
  using WireExecutionReport = schema::futures::ExecutionReportResponse;
  using WireCancelReject = schema::futures::ExecutionReportResponse;
  using WireMassCancelReport = schema::futures::ExecutionReportResponse;
  using WireReject = schema::futures::ApiResponse;

  BinanceFuturesOeMapper(const common::Logger::Producer& logger,
      trading::ResponseManager* response_manager)
      : logger_(logger), response_manager_(response_manager) {}

  [[nodiscard]] trading::ExecutionReport* to_execution_report(
      const WireExecutionReport& msg) const {
    auto* report = allocate_execution_report();
    if (!report) {
      logger_.error("Failed to allocate execution report");
      return nullptr;
    }

    const auto& event = msg.event;
    report->cl_order_id = common::OrderId{event.client_order_id};
    report->symbol = event.symbol;
    report->exec_type = trading::toType(event.execution_type);
    report->ord_status = trading::toOrderStatus(event.order_status);
    report->cum_qty = common::Qty{event.cumulative_filled_quantity};
    report->leaves_qty = common::Qty{
        std::max(0.0, event.order_quantity - event.cumulative_filled_quantity)};
    report->last_qty = common::Qty{event.last_executed_quantity};
    report->price = common::Price{event.order_price};
    report->side = common::toSide(event.side);
    report->text = event.reject_reason;
    report->error_code = 0;

    // Parse position_side from wire response
    if (!event.position_side.empty()) {
      report->position_side = common::toPositionSide(event.position_side);
    }

    // Parse is_maker from wire response
    report->is_maker = event.is_maker;

    return report;
  }

  [[nodiscard]] trading::OrderCancelReject* to_cancel_reject(
      const WireCancelReject& msg) const {
    auto* reject = allocate_cancel_reject();
    if (!reject) {
      logger_.error("Failed to allocate cancel reject");
      return nullptr;
    }
    reject->cl_order_id = common::OrderId{msg.event.client_order_id};
    reject->symbol = msg.event.symbol;
    reject->error_code = 0;
    reject->text = msg.event.reject_reason;
    return reject;
  }

  [[nodiscard]] trading::OrderMassCancelReport* to_mass_cancel_report(
      const WireMassCancelReport& msg) const {
    auto* report = allocate_mass_cancel_report();
    if (!report) {
      logger_.error("Failed to allocate mass cancel report");
      return nullptr;
    }
    report->cl_order_id =
        common::OrderId{static_cast<uint64_t>(msg.event.client_order_id)};
    report->symbol = msg.event.symbol;
    report->mass_cancel_request_type = '7';
    report->mass_cancel_response =
        trading::MassCancelResponse::kCancelSymbolOrders;
    report->total_affected_orders = 0;
    report->error_code = 0;
    report->text = msg.event.reject_reason;
    return report;
  }

  [[nodiscard]] trading::OrderReject to_reject(const WireReject& msg) const {
    trading::OrderReject reject;
    reject.session_reject_reason = "WebSocket";
    reject.rejected_message_type = 0;
    if (msg.error.has_value()) {
      reject.error_code = msg.error.value().code;
      reject.error_message = msg.error.value().message;
    }
    return reject;
  }

 private:
  [[nodiscard]] trading::ExecutionReport* allocate_execution_report() const {
    return response_manager_->execution_report_allocate();
  }
  [[nodiscard]] trading::OrderCancelReject* allocate_cancel_reject() const {
    return response_manager_->order_cancel_reject_allocate();
  }
  [[nodiscard]] trading::OrderMassCancelReport* allocate_mass_cancel_report()
      const {
    return response_manager_->order_mass_cancel_report_allocate();
  }

  const common::Logger::Producer& logger_;
  trading::ResponseManager* response_manager_;
};

}  // namespace core
#endif  // BINANCE_FUTURES_OE_MAPPER_H
