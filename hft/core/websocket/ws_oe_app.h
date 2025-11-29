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

#ifndef WS_ORDER_ENTRY_APP_H
#define WS_ORDER_ENTRY_APP_H

#include "common/authorization.h"
#include "common/logger.h"
#include "common/spsc_queue.h"
#include "core/order_entry.h"
#include "core/websocket/schema/execution_report.h"
#include "schema/account_position.h"
#include "schema/response/api_response.h"
#include "ws_oe_core.h"
#include "ws_transport.h"

namespace trading {
class ResponseManager;
}

namespace core {

class WsOrderEntryApp {
 public:
  using WireMessage = WsOeCore::WireMessage;
  using WireExecutionReport = WsOeCore::WireExecutionReport;
  using WireCancelReject = WsOeCore::WireCancelReject;
  using WireMassCancelReport = WsOeCore::WireMassCancelReport;
  using WireReject = WsOeCore::WireReject;
  using MsgType = std::string;

  WsOrderEntryApp(const std::string& sender_comp_id,
      const std::string& target_comp_id, common::Logger* logger,
      trading::ResponseManager* response_manager);
  ~WsOrderEntryApp();

  bool start();
  void stop();

  bool send(const std::string& msg) const;

  void register_callback(const MsgType& type,
      std::function<void(const WireMessage&)> callback);

  std::string create_log_on_message(const std::string& sig_b64,
      const std::string& timestamp);
  std::string create_log_out_message();
  std::string create_heartbeat_message(WireMessage message);
  std::string create_order_message(
      const trading::NewSingleOrderData& order_data);
  std::string create_cancel_order_message(
      const trading::OrderCancelRequest& cancel_request);
  std::string create_cancel_and_reorder_message(
      const trading::OrderCancelRequestAndNewOrderSingle& cancel_and_re_order);
  std::string create_order_all_cancel(
      const trading::OrderMassCancelRequest& all_order_cancel);
  trading::ExecutionReport* create_execution_report_message(
      const WireExecutionReport& msg);
  trading::OrderCancelReject* create_order_cancel_reject_message(
      const WireCancelReject& msg);
  trading::OrderMassCancelReport* create_order_mass_cancel_report_message(
      const WireMassCancelReport& msg);
  trading::OrderReject create_reject_message(const WireReject& msg);

  WireMessage decode(const std::string& message);

 private:
  void create_log_on() const;
  void handle_payload(std::string_view payload);
  void dispatch(const std::string& type, const WireMessage& message);
  static std::string get_signature_base64(const std::string& payload);

  void handle_execution_report(const schema::ExecutionReportResponse& ptr);
  void handle_balance_update(const schema::BalanceUpdateEnvelope& ptr) const;
  void handle_account_updated(
      const schema::OutboundAccountPositionEnvelope& ptr) const;
  void handle_session_logon(const schema::SessionLogonResponse& ptr);
  void handle_user_subscription(
      const schema::SessionUserSubscriptionResponse& ptr);
  void handle_api_response(const schema::ApiResponse& ptr);

  common::Logger::Producer logger_;
  core::WsOeCore ws_oe_core_;
  std::unique_ptr<core::WebSocketTransport<"OERead">> transport_;
  std::atomic<bool> running_{false};

  std::unordered_map<MsgType, std::function<void(const WireMessage&)>>
      callbacks_;

  const std::string host_;
  const std::string path_;
  const int port_;
  const bool use_ssl_;
};

}  // namespace core

#endif  //WS_ORDER_ENTRY_APP_H
