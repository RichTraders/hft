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
#include "common/thread.hpp"
#include "core/order_entry.h"
#include "websocket/connection_handler.h"
#include "ws_oe_core.h"
#include "ws_oe_dispatcher_context.h"
#include "ws_order_manager.hpp"
#include "ws_transport.h"

#ifdef USE_FUTURES_API
#include "exchanges/binance/futures/binance_futures_oe_traits.h"
#include "exchanges/binance/futures/futures_ws_oe_decoder.h"
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
      const std::string& target_comp_id, const common::Logger::Producer& logger,
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
  [[nodiscard]] std::string create_modify_order_message(
      const trading::OrderModifyRequest& modify_request) const;
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
  void post_modify_order(const trading::OrderModifyRequest& data);
  void post_mass_cancel_order(const trading::OrderMassCancelRequest& data);

  void dispatch(const std::string& type, const WireMessage& message) const;

  [[nodiscard]] std::string create_user_data_stream_subscribe() const {
    return ws_oe_core_.create_user_data_stream_subscribe();
  }

  void handle_stream_payload(std::string_view payload);
  void handle_listen_key_response(const std::string& listen_key);
  void initiate_session_logon();
  void start_listen_key_keepalive() { start_keepalive_impl(keepalive_thread_); }

 private:
  void handle_api_payload(std::string_view payload);
  static std::string get_signature_base64(const std::string& payload);

  void stop_stream_transport_impl(
      std::unique_ptr<WebSocketTransport<"OEStream">>& transport) {
    if (transport) {
      transport->interrupt();
    }
  }
  void stop_stream_transport_impl(std::monostate&) {}

  void start_stream_transport_impl(
      std::unique_ptr<WebSocketTransport<"OEStream">>& transport,
      const std::string& listen_key) {
    listen_key_ = listen_key;
    logger_.info("[WsOeApp] Received listenKey, connecting stream transport");

    const std::string stream_host =
        std::string(WsOeCoreImpl::ExchangeTraits::get_stream_host());
    const std::string stream_path =
        std::string(WsOeCoreImpl::ExchangeTraits::get_stream_endpoint_path()) +
        "?listenKey=" + listen_key_;
    const int stream_port = WsOeCoreImpl::ExchangeTraits::get_stream_port();

    transport->register_message_callback([this](std::string_view payload) {
      this->handle_stream_payload(payload);
    });

    transport->initialize(stream_host,
        stream_port,
        stream_path,
        use_ssl_,
        false);

    logger_.info("[WsOeApp] Stream transport connected");
  }
  void start_stream_transport_impl(std::monostate&, const std::string&) {}

  // Helper type for conditional stream transport
  using OptionalStreamTransport = std::conditional_t<
      WsOeCoreImpl::ExchangeTraits::requires_stream_transport(),
      std::unique_ptr<WebSocketTransport<"OEStream">>, std::monostate>;

  const common::Logger::Producer& logger_;
  WsOeCoreImpl ws_oe_core_;
  WsOrderManager<WsOeCoreImpl::ExchangeTraits> ws_order_manager_;
  WsOeDispatchContext<WsOeCoreImpl::ExchangeTraits> dispatch_context_;
  std::unique_ptr<WebSocketTransport<"OEApi">> api_transport_;
  std::atomic<bool> running_{false};

  std::unordered_map<MsgType, std::function<void(const WireMessage&)>>
      callbacks_;

  const std::string host_;
  const std::string path_;
  const int port_;
  const bool use_ssl_;

  [[no_unique_address]] OptionalStreamTransport stream_transport_;
  std::string listen_key_;

  using OptionalKeepaliveThread =
      std::conditional_t<WsOeCoreImpl::ExchangeTraits::requires_listen_key(),
          std::unique_ptr<common::Thread<"ListenKeyOE">>, std::monostate>;

  [[no_unique_address]] OptionalKeepaliveThread keepalive_thread_;
  std::atomic<bool> keepalive_running_{false};

  void start_keepalive_impl(
      std::unique_ptr<common::Thread<"ListenKeyOE">>& thread);
  void start_keepalive_impl(std::monostate&) {}
  void stop_keepalive_impl(
      std::unique_ptr<common::Thread<"ListenKeyOE">>& thread);
  void stop_keepalive_impl(std::monostate&) {}
  void keepalive_loop();

  // static OptionalStreamTransport create_stream_transport() {
  //   if constexpr (WsOeCoreImpl::ExchangeTraits::requires_stream_transport()) {
  //     return std::make_unique<WebSocketTransport<"OEStream">>();
  //   } else {
  //     return std::monostate{};
  //   }
  // }
};

}  // namespace core

#endif  //WS_ORDER_ENTRY_APP_H
