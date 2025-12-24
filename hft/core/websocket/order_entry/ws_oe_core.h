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

#include <string>
#include <string_view>

#include "common/logger.h"
#include "core/order_entry.h"
#include "oe_exchange_traits.h"

namespace trading {
class ResponseManager;
}

namespace core {

template <OeExchangeTraits Traits, typename DecoderType>
class WsOeCore {
 public:
  using ExchangeTraits = Traits;
  using Decoder = DecoderType;
  using WireMessage = ExchangeTraits::WireMessage;
  using WireExecutionReport = ExchangeTraits::ExecutionReportResponse;
  using WireCancelReject = ExchangeTraits::ExecutionReportResponse;
  using WireMassCancelReport = ExchangeTraits::ExecutionReportResponse;
  using WireReject = ExchangeTraits::ApiResponse;

  WsOeCore(const common::Logger::Producer& logger,
      trading::ResponseManager* response_manager)
      : logger_(logger),
        mapper_(logger_, response_manager),
        decoder_(logger_),
        encoder_(logger_),
        response_manager_(response_manager) {}

  ~WsOeCore() = default;

  [[nodiscard]] std::string create_log_on_message(const std::string& signature,
      const std::string& timestamp) const;
  [[nodiscard]] std::string create_log_out_message() const;
  [[nodiscard]] std::string create_heartbeat_message() const;

  [[nodiscard]] std::string create_user_data_stream_subscribe() const;
  [[nodiscard]] std::string create_user_data_stream_unsubscribe() const;
  [[nodiscard]] std::string create_user_data_stream_ping() const;

  [[nodiscard]] std::string create_order_message(
      const trading::NewSingleOrderData& order) const;
  [[nodiscard]] std::string create_cancel_order_message(
      const trading::OrderCancelRequest& cancel) const;
  [[nodiscard]] std::string create_cancel_and_reorder_message(
      const trading::OrderCancelAndNewOrderSingle& replace) const;
  [[nodiscard]] std::string create_modify_order_message(
      const trading::OrderModifyRequest& modify) const;
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
  const common::Logger::Producer& logger_;
  ExchangeTraits::Mapper mapper_;
  Decoder decoder_;
  ExchangeTraits::Encoder encoder_;
  trading::ResponseManager* response_manager_;
};

}  // namespace core

#include "ws_oe_core.tpp"

#endif
