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
#include "core/websocket/schema/execution_report.h"
#include "schema/account_position.h"
#include "schema/response/api_response.h"
#include "schema/response/order.h"
#include "schema/response/session_response.h"

namespace trading {
class ResponseManager;
}

namespace core {

class WsOeCore {
 public:
  using WireMessage = std::variant<std::monostate,
      schema::ExecutionReportResponse, schema::SessionLogonResponse,
      schema::CancelOrderResponse, schema::CancelAllOrdersResponse,
      schema::SessionUserSubscriptionResponse,
      schema::SessionUserUnsubscriptionResponse,
      schema::CancelAndReorderResponse, schema::PlaceOrderResponse,
      schema::BalanceUpdateEnvelope, schema::OutboundAccountPositionEnvelope,
      schema::ApiResponse>;
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

  std::string create_order_message(const trading::NewSingleOrderData& order);
  std::string create_cancel_order_message(
      const trading::OrderCancelRequest& cancel);
  std::string create_cancel_and_reorder_message(
      const trading::OrderCancelRequestAndNewOrderSingle& replace);
  std::string create_order_all_cancel(
      const trading::OrderMassCancelRequest& request);

  trading::ExecutionReport* create_execution_report_message(
      const WireExecutionReport& msg) const;
  trading::OrderCancelReject* create_order_cancel_reject_message(
      const WireCancelReject& msg) const;
  trading::OrderMassCancelReport* create_order_mass_cancel_report_message(
      const WireMassCancelReport& msg) const;
  trading::OrderReject create_reject_message(const WireReject& msg) const;
  [[nodiscard]] WireMessage decode(std::string_view payload);
  template <class T>
  WireMessage decode_or_log(std::string_view payload, std::string_view label);

 private:
  static constexpr int kPricePrecision = 2;
  static constexpr int kQtyPrecision = 5;
  trading::ExecutionReport* allocate_execution_report() const;
  trading::OrderCancelReject* allocate_cancel_reject() const;
  trading::OrderMassCancelReport* allocate_mass_cancel_report() const;

  common::Logger::Producer logger_;
  trading::ResponseManager* response_manager_;
  mutable uint64_t request_sequence_{0};

  struct Decoder {
    std::string_view token;
    WireMessage (*fn)(std::string_view);
  };

  template <typename T>
  std::string to_fixed(T data, int precision) {
    static constexpr size_t kPrecisionBufferSize = 32;
    std::array<char, kPrecisionBufferSize> buf;
    auto [ptr, ec] = std::to_chars(buf.data(),
        buf.data() + sizeof(buf),
        data,
        std::chars_format::fixed,
        precision);
    assert(ec == std::errc());

    return std::string(buf.data(), ptr);
  }
};

}  // namespace core

#endif
