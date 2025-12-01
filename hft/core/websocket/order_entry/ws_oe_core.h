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

#ifndef WS_OE_CORE_H
#define WS_OE_CORE_H

#include <variant>

#include "common/logger.h"
#include "core/order_entry.h"
#include "ws_oe_decoder.h"
#include "ws_oe_encoder.h"
#include "ws_oe_mapper.h"

namespace trading {
class ResponseManager;
}

namespace core {

class WsOeCore {
 public:
  using WireMessage = WsOeWireMessage;
  using WireExecutionReport = schema::ExecutionReportResponse;
  using WireCancelReject = schema::ExecutionReportResponse;
  using WireMassCancelReport = schema::ExecutionReportResponse;
  using WireReject = schema::ApiResponse;

  WsOeCore(common::Logger* logger, trading::ResponseManager* response_manager);
  ~WsOeCore();

  std::string create_log_on_message(const std::string& signature,
      const std::string& timestamp) const;
  std::string create_log_out_message() const;
  std::string create_heartbeat_message() const;

  std::string create_user_data_stream_subscribe() const;
  std::string create_user_data_stream_unsubscribe() const;

  [[nodiscard]] std::string create_order_message(
      const trading::NewSingleOrderData& order) const;
  [[nodiscard]] std::string create_cancel_order_message(
      const trading::OrderCancelRequest& cancel) const;
  [[nodiscard]] std::string create_cancel_and_reorder_message(
      const trading::OrderCancelRequestAndNewOrderSingle& replace) const;
  [[nodiscard]] std::string create_order_all_cancel(
      const trading::OrderMassCancelRequest& request) const;

  trading::ExecutionReport* create_execution_report_message(
      const WireExecutionReport& msg) const;
  trading::OrderCancelReject* create_order_cancel_reject_message(
      const WireCancelReject& msg) const;
  trading::OrderMassCancelReport* create_order_mass_cancel_report_message(
      const WireMassCancelReport& msg) const;
  trading::OrderReject create_reject_message(const WireReject& msg) const;
  [[nodiscard]] WireMessage decode(std::string_view payload) const;

 private:
  common::Logger::Producer logger_;
  WsOeDomainMapper mapper_;
  WsOeDecoder decoder_;
  WsOeEncoder encoder_;
  trading::ResponseManager* response_manager_;
  mutable uint64_t request_sequence_{0};
};

}  // namespace core

#endif
