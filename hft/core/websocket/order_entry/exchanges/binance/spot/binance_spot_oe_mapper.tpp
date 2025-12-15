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

namespace core {

inline trading::ExecutionReport* BinanceSpotOeMapper::to_execution_report(
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

  return report;
}

inline trading::OrderCancelReject* BinanceSpotOeMapper::to_cancel_reject(
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

inline trading::OrderMassCancelReport* BinanceSpotOeMapper::to_mass_cancel_report(
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

inline trading::OrderReject BinanceSpotOeMapper::to_reject(const WireReject& msg) const {
  trading::OrderReject reject;
  reject.session_reject_reason = "WebSocket";
  reject.rejected_message_type = 0;
  if (msg.error.has_value()) {
    reject.error_code = msg.error.value().code;
    reject.error_message = msg.error.value().message;
  }
  return reject;
}

inline trading::ExecutionReport* BinanceSpotOeMapper::allocate_execution_report() const {
  return response_manager_->execution_report_allocate();
}

inline trading::OrderCancelReject* BinanceSpotOeMapper::allocate_cancel_reject() const {
  return response_manager_->order_cancel_reject_allocate();
}

inline trading::OrderMassCancelReport* BinanceSpotOeMapper::allocate_mass_cancel_report()
    const {
  return response_manager_->order_mass_cancel_report_allocate();
}
}  // namespace core
