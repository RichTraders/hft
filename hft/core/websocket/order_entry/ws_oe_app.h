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

#include "common/logger.h"
#include "core/order_entry.h"
#include "ws_oe_core.h"
#include "ws_order_manager.h"
#include "ws_transport.h"

#ifdef USE_FUTURES_API
#include "exchanges/binance/futures/binance_futures_oe_traits.h"
#include "futures_ws_oe_decoder.h"
#else
#include "exchanges/binance/spot/binance_spot_oe_traits.h"
#include "spot_ws_oe_decoder.h"
#endif

namespace trading {
class ResponseManager;
}

namespace core {

#ifdef ENABLE_SBE_DECODER_ORDER_ENTRY
#ifdef USE_FUTURES_API
static_assert(false, "SBE not supported for Futures Order Entry");
#else
static_assert(false, "SBE not supported for Spot Order Entry");
#endif
#else
#ifdef USE_FUTURES_API
using WsOeCoreImpl = WsOeCore<BinanceFuturesOeTraits, FuturesWsOeDecoder>;
#else
using WsOeCoreImpl = WsOeCore<BinanceSpotOeTraits, SpotWsOeDecoder>;
#endif
#endif

class WsOrderEntryApp {
 public:
  using WireMessage = WsOeCoreImpl::WireMessage;
  using WireExecutionReport = WsOeCoreImpl::WireExecutionReport;
  using WireCancelReject = WsOeCoreImpl::WireCancelReject;
  using WireMassCancelReport = WsOeCoreImpl::WireMassCancelReport;
  using WireReject = WsOeCoreImpl::WireReject;
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

  [[nodiscard]] std::string create_log_on_message(const std::string& sig_b64,
      const std::string& timestamp) const;
  [[nodiscard]] std::string create_log_out_message() const;
  [[nodiscard]] std::string create_heartbeat_message(WireMessage message) const;
  [[nodiscard]] std::string create_order_message(
      const trading::NewSingleOrderData& order_data) const;
  [[nodiscard]] std::string create_cancel_order_message(
      const trading::OrderCancelRequest& cancel_request) const;
  [[nodiscard]] std::string create_cancel_and_reorder_message(
      const trading::OrderCancelAndNewOrderSingle& cancel_and_re_order) const;
  [[nodiscard]] std::string create_order_all_cancel(
      const trading::OrderMassCancelRequest& all_order_cancel) const;

  [[nodiscard]] trading::ExecutionReport* create_execution_report_message(
      const WireExecutionReport& msg) const;
  [[nodiscard]] trading::OrderCancelReject* create_order_cancel_reject_message(
      const WireCancelReject& msg) const;
  [[nodiscard]] trading::OrderMassCancelReport*
  create_order_mass_cancel_report_message(
      const WireMassCancelReport& msg) const;
  [[nodiscard]] trading::OrderReject create_reject_message(
      const WireReject& msg) const;

  WireMessage decode(const std::string& message);

  void post_new_order(const trading::NewSingleOrderData& data);
  void post_cancel_order(const trading::OrderCancelRequest& data);
  void post_cancel_and_reorder(
      const trading::OrderCancelAndNewOrderSingle& data);
  void post_mass_cancel_order(const trading::OrderMassCancelRequest& data);

 private:
  void create_log_on() const;
  void handle_payload(std::string_view payload);
  void dispatch(const std::string& type, const WireMessage& message) const;
  static std::string get_signature_base64(const std::string& payload);

  void handle_execution_report(const schema::ExecutionReportResponse& ptr);
  void handle_balance_update(const schema::BalanceUpdateEnvelope& ptr) const;
  void handle_account_updated(
      const schema::OutboundAccountPositionEnvelope& ptr) const;
  void handle_session_logon(const schema::SessionLogonResponse& ptr) const;
  void handle_user_subscription(
      const schema::SessionUserSubscriptionResponse& ptr);
  void handle_api_response(const schema::ApiResponse& ptr);
  void handle_cancel_and_reorder_response(
      const schema::CancelAndReorderResponse& ptr);
  void handle_cancel_all_response(const schema::CancelAllOrdersResponse& ptr);
  void handle_place_order_response(const schema::PlaceOrderResponse& ptr);

  common::Logger::Producer logger_;
  WsOeCoreImpl ws_oe_core_;
  WsOrderManager ws_order_manager_;
  std::unique_ptr<WebSocketTransport<"OERead">> transport_;
  std::atomic<bool> running_{false};

  std::unordered_map<MsgType, std::function<void(const WireMessage&)>>
      callbacks_;

  const std::string host_;
  const std::string path_;
  const int port_;
  const bool use_ssl_;

  std::unique_ptr<WsOeCoreImpl::ExchangeTraits::ListenKeyManager>
      listen_key_manager_;
};

}  // namespace core

#endif  //WS_ORDER_ENTRY_APP_H
