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
#include "common/thread.hpp"
#include "core/common.h"
#include "core/order_entry.h"
#include "core/signature.h"
#include "performance.h"
#include "schema/spot/response/account_position.h"
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

namespace detail {
constexpr int kDefaultRecvWindow = 5000;

inline std::string get_signature_base64_impl(std::string_view timestamp_ms,
    std::uint32_t recv_window_ms = kDefaultRecvWindow) {
  EVP_PKEY* private_key =
      Util::load_ed25519(AUTHORIZATION.get_pem_file_path(),
          AUTHORIZATION.get_private_password().c_str());

  std::vector<std::pair<std::string, std::string>> params;
  params.emplace_back("apiKey", AUTHORIZATION.get_api_key());
  params.emplace_back("timestamp", std::string(timestamp_ms));
  if (recv_window_ms > 0) {
    params.emplace_back("recvWindow", std::to_string(recv_window_ms));
  }
  const std::string payload = Util::build_canonical_query(std::move(params));

  auto result = Util::sign_and_base64(private_key, payload);
  Util::free_key(private_key);
  return result;
}
}  // namespace detail

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

using DefaultOeApiTransport = WebSocketTransport<"OEApi">;
using DefaultOeStreamTransport = WebSocketTransport<"OEStream">;

template <typename ApiTransportT = DefaultOeApiTransport,
          typename StreamTransportT = DefaultOeStreamTransport>
class WsOrderEntryAppT {
 public:
  using ApiTransportType = ApiTransportT;
  using StreamTransportType = StreamTransportT;
  using WireMessage = typename WsOeCoreImpl::WireMessage;
  using WireExecutionReport = typename WsOeCoreImpl::WireExecutionReport;
  using WireCancelReject = typename WsOeCoreImpl::WireCancelReject;
  using WireMassCancelReport = typename WsOeCoreImpl::WireMassCancelReport;
  using WireReject = typename WsOeCoreImpl::WireReject;
  using MsgType = std::string;

 private:
  using OptionalStreamTransport = std::conditional_t<
      WsOeCoreImpl::ExchangeTraits::requires_stream_transport(),
      std::unique_ptr<StreamTransportType>, std::monostate>;

  using DispatchContextType = WsOeDispatchContext<
      typename WsOeCoreImpl::ExchangeTraits,
      WsOrderEntryAppT<ApiTransportT, StreamTransportT>>;

  using OptionalKeepaliveThread =
      std::conditional_t<WsOeCoreImpl::ExchangeTraits::requires_listen_key(),
          std::unique_ptr<common::Thread<"ListenKeyOE">>, std::monostate>;

 public:
  WsOrderEntryAppT(const std::string& /*sender_comp_id*/,
      const std::string& /*target_comp_id*/,
      const common::Logger::Producer& logger,
      trading::ResponseManager* response_manager)
      : logger_(logger),
        ws_oe_core_(logger_, response_manager),
        ws_order_manager_(logger_),
        dispatch_context_(&logger_, &ws_order_manager_, this),
        host_(std::string(WsOeCoreImpl::ExchangeTraits::get_api_host())),
        path_(std::string(WsOeCoreImpl::ExchangeTraits::get_api_endpoint_path())),
        port_(WsOeCoreImpl::ExchangeTraits::get_api_port()),
        use_ssl_(WsOeCoreImpl::ExchangeTraits::use_ssl()),
        stream_transport_([] {
          if constexpr (WsOeCoreImpl::ExchangeTraits::requires_stream_transport()) {
            return std::make_unique<StreamTransportType>();
          } else {
            return std::monostate{};
          }
        }()) {}

  ~WsOrderEntryAppT() { stop(); }

  bool start() {
    // NOLINTNEXTLINE(readability-simplify-boolean-expr)
    if (running_.exchange(true)) {
      return false;
    }

    api_transport_ = std::make_unique<ApiTransportType>(host_,
        port_, path_, use_ssl_, true);

    api_transport_->register_message_callback(
        [this](std::string_view payload) { this->handle_api_payload(payload); });
    return true;
  }

  void stop() {
    if (!running_.exchange(false)) {
      return;
    }

    stop_keepalive_impl(keepalive_thread_);
    stop_stream_transport_impl(stream_transport_);

    if (api_transport_) {
      api_transport_->interrupt();
    }
    api_transport_.reset();
  }

  [[nodiscard]] bool send(const std::string& msg) const {
    if (!api_transport_ || msg.empty()) {
      return false;
    }
    logger_.info("[WsOrderEntryApp] Sending message to server :{}", msg);
    return api_transport_->write(msg) >= 0;
  }

  void register_callback(const MsgType& type,
      std::function<void(const WireMessage&)> callback) {
    callbacks_[type] = std::move(callback);
  }

  [[nodiscard]] std::string create_log_on_message(const std::string& sig_b64,
      const std::string& timestamp) const {
    return ws_oe_core_.create_log_on_message(sig_b64, timestamp);
  }

  [[nodiscard]] std::string create_log_out_message() const {
    return ws_oe_core_.create_log_out_message();
  }

  // NOLINTBEGIN(performance-unnecessary-value-param)
  [[nodiscard]] std::string create_heartbeat_message(WireMessage /*message*/) const {
    return ws_oe_core_.create_heartbeat_message();
  }
  // NOLINTEND(performance-unnecessary-value-param)

  [[nodiscard]] std::string create_order_message(
      const trading::NewSingleOrderData& order_data) const {
    return ws_oe_core_.create_order_message(order_data);
  }

  [[nodiscard]] std::string create_cancel_order_message(
      const trading::OrderCancelRequest& cancel_request) const {
    return ws_oe_core_.create_cancel_order_message(cancel_request);
  }

  [[nodiscard]] std::string create_cancel_and_reorder_message(
      const trading::OrderCancelAndNewOrderSingle& cancel_and_re_order) const {
    return ws_oe_core_.create_cancel_and_reorder_message(cancel_and_re_order);
  }

  [[nodiscard]] std::string create_modify_order_message(
      const trading::OrderModifyRequest& modify_request) const {
    return ws_oe_core_.create_modify_order_message(modify_request);
  }

  [[nodiscard]] std::string create_order_all_cancel(
      const trading::OrderMassCancelRequest& all_order_cancel) const {
    return ws_oe_core_.create_order_all_cancel(all_order_cancel);
  }

  [[nodiscard]] trading::ExecutionReport* create_execution_report_message(
      const WireExecutionReport& msg) const {
    return ws_oe_core_.create_execution_report_message(msg);
  }

  [[nodiscard]] trading::OrderCancelReject* create_order_cancel_reject_message(
      const WireCancelReject& msg) const {
    return ws_oe_core_.create_order_cancel_reject_message(msg);
  }

  [[nodiscard]] trading::OrderMassCancelReport*
  create_order_mass_cancel_report_message(const WireMassCancelReport& msg) const {
    return ws_oe_core_.create_order_mass_cancel_report_message(msg);
  }

  [[nodiscard]] trading::OrderReject create_reject_message(
      const WireReject& msg) const {
    return ws_oe_core_.create_reject_message(msg);
  }

  WireMessage decode(const std::string& message) {
    return ws_oe_core_.decode(message);
  }

  void post_new_order(const trading::NewSingleOrderData& data) {
    PendingOrderRequest request;
    request.client_order_id = data.cl_order_id.value;
    request.symbol = data.symbol;
    request.side = trading::to_common_side(data.side);
    request.price = data.price;
    request.order_qty = data.order_qty;
    request.ord_type = data.ord_type;
    request.time_in_force = data.time_in_force;
    request.position_side = data.position_side;
    ws_order_manager_.register_pending_request(request);
  }

  void post_cancel_order(const trading::OrderCancelRequest& data) {
    PendingOrderRequest request;
    request.client_order_id = data.cl_order_id.value;
    request.symbol = data.symbol;
    request.position_side = data.position_side;
    ws_order_manager_.register_pending_request(request);
  }

  void post_cancel_and_reorder(
      const trading::OrderCancelAndNewOrderSingle& data) {
    PendingOrderRequest request;
    request.client_order_id = data.cl_new_order_id.value;
    request.symbol = data.symbol;
    request.side = trading::to_common_side(data.side);
    request.price = data.price;
    request.order_qty = data.order_qty;
    request.ord_type = data.ord_type;
    request.time_in_force = data.time_in_force;
    request.position_side = data.position_side;
    ws_order_manager_.register_pending_request(request);
  }

  void post_modify_order(const trading::OrderModifyRequest& data) {
    PendingOrderRequest request;
    request.client_order_id = data.orig_client_order_id.value;
    request.symbol = data.symbol;
    request.side = trading::to_common_side(data.side);
    request.price = data.price;
    request.order_qty = data.order_qty;
    request.ord_type = trading::OrderType::kLimit;
    request.time_in_force = trading::TimeInForce::kGoodTillCancel;
    request.position_side = data.position_side;
    ws_order_manager_.register_pending_request(request);
  }

  void post_mass_cancel_order(const trading::OrderMassCancelRequest& data) {
    PendingOrderRequest request;
    request.client_order_id = data.cl_order_id.value;
    request.symbol = data.symbol;
    ws_order_manager_.register_pending_request(request);
  }

  void dispatch(const std::string& type, const WireMessage& message) const {
    const auto callback = callbacks_.find(type);
    if (callback == callbacks_.end() || !callback->second) {
      logger_.warn("No callback registered for message type {}", type);
      return;
    }
    callback->second(message);
  }

  [[nodiscard]] std::string create_user_data_stream_subscribe() const {
    return ws_oe_core_.create_user_data_stream_subscribe();
  }

  void handle_stream_payload(std::string_view payload) {
    if constexpr (WsOeCoreImpl::ExchangeTraits::requires_stream_transport()) {
      if (payload.empty()) {
        return;
      }

      if (payload == "__CONNECTED__") {
        using ConnectionHandler = typename WsOeCoreImpl::ExchangeTraits::ConnectionHandler;
        ConnectionContext<WsOrderEntryAppT> ctx(this, TransportId::kStream);
        ConnectionHandler::on_connected(ctx, TransportId::kStream);
        return;
      }

      constexpr int kDefaultLogLen = 200;
      logger_.debug("[WsOrderEntryApp]Received stream payload (size: {}): {}...",
          payload.size(),
          payload.substr(0, std::min<size_t>(kDefaultLogLen, payload.size())));

      START_MEASURE(Convert_Stream_Message);
      const WireMessage message = ws_oe_core_.decode(payload);
      END_MEASURE(Convert_Stream_Message, logger_);

      if (UNLIKELY(std::holds_alternative<std::monostate>(message))) {
        return;
      }

      WsOeCoreImpl::ExchangeTraits::DispatchRouter::template process_message<
          typename WsOeCoreImpl::ExchangeTraits>(message, dispatch_context_);
    } else {
      (void)payload;
    }
  }

  void handle_listen_key_response(const std::string& listen_key) {
    start_stream_transport_impl(stream_transport_, listen_key);
  }

  void initiate_session_logon() {
    if constexpr (WsOeCoreImpl::ExchangeTraits::requires_signature_logon()) {
      const auto cur_timestamp = std::to_string(util::get_timestamp_epoch());
      const std::string sig_b64 = detail::get_signature_base64_impl(cur_timestamp, 0);
      auto log_on_message =
          ws_oe_core_.create_log_on_message(sig_b64, cur_timestamp);
      api_transport_->write(log_on_message);
    }
  }

  void start_listen_key_keepalive() { start_keepalive_impl(keepalive_thread_); }

  [[nodiscard]] bool is_session_ready() const noexcept {
    return session_ready_.load(std::memory_order_acquire);
  }

  void set_session_ready() noexcept {
    session_ready_.store(true, std::memory_order_release);
    logger_.info("[WsOeApp] Session ready");
  }

  ApiTransportType& api_transport() { return *api_transport_; }
  const ApiTransportType& api_transport() const { return *api_transport_; }

  template <typename T = StreamTransportType>
    requires (!std::is_same_v<T, std::monostate>)
  auto stream_transport() -> T& {
    return *stream_transport_;
  }

 private:
  void handle_api_payload(std::string_view payload) {
    if (payload.empty()) {
      return;
    }
    if (payload == "__CONNECTED__") {
      using ConnectionHandler = typename WsOeCoreImpl::ExchangeTraits::ConnectionHandler;
      ConnectionContext<WsOrderEntryAppT> ctx(this, TransportId::kApi);
      ConnectionHandler::on_connected(ctx, TransportId::kApi);
      return;
    }

    constexpr int kDefaultLogLen = 200;
    logger_.debug("[WsOrderEntryApp]Received payload (size: {}): {}...",
        payload.size(),
        payload.substr(0, std::min<size_t>(kDefaultLogLen, payload.size())));

    START_MEASURE(Convert_Message);
    const WireMessage message = ws_oe_core_.decode(payload);
    END_MEASURE(Convert_Message, logger_);

    if (UNLIKELY(std::holds_alternative<std::monostate>(message))) {
      return;
    }

    WsOeCoreImpl::ExchangeTraits::DispatchRouter::template process_message<
        typename WsOeCoreImpl::ExchangeTraits>(message, dispatch_context_);
  }

  static std::string get_signature_base64(const std::string& payload) {
    EVP_PKEY* private_key = Util::load_ed25519(AUTHORIZATION.get_pem_file_path(),
        AUTHORIZATION.get_private_password().c_str());

    const std::string signature = Util::sign_and_base64(private_key, payload);

    Util::free_key(private_key);
    return signature;
  }

  void stop_stream_transport_impl(std::unique_ptr<StreamTransportType>& transport) {
    if (transport) {
      transport->interrupt();
    }
  }
  void stop_stream_transport_impl(std::monostate&) {}

  void start_stream_transport_impl(
      std::unique_ptr<StreamTransportType>& transport,
      const std::string& listen_key) {
    listen_key_ = listen_key;
    logger_.info("[WsOeApp] Received listenKey, connecting stream transport");

    const std::string stream_host =
        std::string(WsOeCoreImpl::ExchangeTraits::get_stream_host());
    const std::string stream_path =
        std::string(WsOeCoreImpl::ExchangeTraits::get_stream_endpoint_path()) +
        "/" + listen_key_;
    const int stream_port = WsOeCoreImpl::ExchangeTraits::get_stream_port();

    transport->register_message_callback([this](std::string_view payload) {
      this->handle_stream_payload(payload);
    });

    transport->initialize(stream_host, stream_port, stream_path, use_ssl_, false);

    logger_.info("[WsOeApp] Stream transport connected");
  }
  void start_stream_transport_impl(std::monostate&, const std::string&) {}

  void start_keepalive_impl(std::unique_ptr<common::Thread<"ListenKeyOE">>& thread) {
    if (keepalive_running_.exchange(true)) {
      return;
    }

    thread = std::make_unique<common::Thread<"ListenKeyOE">>();
    thread->start([this]() { keepalive_loop(); });
    logger_.info("[WsOeApp] Listen key keepalive thread started");
  }
  void start_keepalive_impl(std::monostate&) {}

  void stop_keepalive_impl(std::unique_ptr<common::Thread<"ListenKeyOE">>& thread) {
    if (!keepalive_running_.exchange(false)) {
      return;
    }

    if (thread) {
      thread->join();
      thread.reset();
    }
    logger_.info("[WsOeApp] Listen key keepalive thread stopped");
  }
  void stop_keepalive_impl(std::monostate&) {}

  void keepalive_loop() {
    if constexpr (!WsOeCoreImpl::ExchangeTraits::requires_listen_key()) {
      return;
    }

    const int keepalive_interval_ms =
        WsOeCoreImpl::ExchangeTraits::get_keepalive_interval_ms();
    constexpr int kSleepIntervalMs = 1000;

    int elapsed_ms = 0;

    while (keepalive_running_.load(std::memory_order_relaxed)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(kSleepIntervalMs));
      elapsed_ms += kSleepIntervalMs;

      if (elapsed_ms >= keepalive_interval_ms) {
        elapsed_ms = 0;

        if (!api_transport_) {
          logger_.warn("[WsOeApp] API transport not available for keepalive");
          continue;
        }

        const std::string ping_msg = ws_oe_core_.create_user_data_stream_ping();
        if (!ping_msg.empty()) {
          api_transport_->write(ping_msg);
          logger_.trace("[WsOeApp] Sent userDataStream.ping keepalive");
        }
      }
    }
  }

  const common::Logger::Producer& logger_;
  WsOeCoreImpl ws_oe_core_;
  WsOrderManager<typename WsOeCoreImpl::ExchangeTraits> ws_order_manager_;
  DispatchContextType dispatch_context_;
  std::unique_ptr<ApiTransportType> api_transport_;
  std::atomic<bool> running_{false};

  std::unordered_map<MsgType, std::function<void(const WireMessage&)>> callbacks_;

  const std::string host_;
  const std::string path_;
  const int port_;
  const bool use_ssl_;

  [[no_unique_address]] OptionalStreamTransport stream_transport_;
  std::string listen_key_;

  [[no_unique_address]] OptionalKeepaliveThread keepalive_thread_;
  std::atomic<bool> keepalive_running_{false};
  std::atomic<bool> session_ready_{false};
};

using WsOrderEntryApp = WsOrderEntryAppT<>;

}  // namespace core

#endif  //WS_ORDER_ENTRY_APP_H
