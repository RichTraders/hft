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
#include "ws_oe_app.h"

#include "common/authorization.h"
#include "core/common.h"
#include "core/signature.h"
#include "performance.h"
#include "schema/spot/response/account_position.h"

constexpr int kDefaultRecvWindow = 5000;

namespace {
std::string get_signature_base64(std::string_view timestamp_ms,
    std::uint32_t recv_window_ms = kDefaultRecvWindow) {
  EVP_PKEY* private_key =
      core::Util::load_ed25519(AUTHORIZATION.get_pem_file_path(),
          AUTHORIZATION.get_private_password().c_str());

  std::vector<std::pair<std::string, std::string>> params;
  params.emplace_back("apiKey", AUTHORIZATION.get_api_key());
  params.emplace_back("timestamp", std::string(timestamp_ms));
  if (recv_window_ms > 0) {
    params.emplace_back("recvWindow", std::to_string(recv_window_ms));
  }
  const std::string payload =
      core::Util::build_canonical_query(std::move(params));

  auto result = core::Util::sign_and_base64(private_key, payload);
  core::Util::free_key(private_key);
  return result;
}
}  // namespace

namespace core {

WsOrderEntryApp::WsOrderEntryApp(const std::string& /*sender_comp_id*/,
    const std::string& /*target_comp_id*/, common::Logger* logger,
    trading::ResponseManager* response_manager)
    : logger_(logger->make_producer()),
      ws_oe_core_(logger_, response_manager),
      ws_order_manager_(logger_),
      dispatch_context_(&logger_, &ws_order_manager_, this),
      host_(std::string(WsOeCoreImpl::ExchangeTraits::get_api_host())),
      path_(std::string(WsOeCoreImpl::ExchangeTraits::get_api_endpoint_path())),
      port_(WsOeCoreImpl::ExchangeTraits::get_api_port()),
      use_ssl_(WsOeCoreImpl::ExchangeTraits::use_ssl()) {}

WsOrderEntryApp::~WsOrderEntryApp() {
  stop();
}

bool WsOrderEntryApp::start() {
  if (running_.exchange(true)) {
    return false;
  }

  api_transport_ = std::make_unique<WebSocketTransport<"OEApi">>(host_,
      port_,
      path_,
      use_ssl_,
      true);

  api_transport_->register_message_callback(
      [this](std::string_view payload) { this->handle_api_payload(payload); });
  return true;
}

void WsOrderEntryApp::stop() {
  if (!running_.exchange(false)) {
    return;
  }

  stop_stream_transport_impl(stream_transport_);

  if (api_transport_) {
    api_transport_->interrupt();
  }
  api_transport_.reset();
}

bool WsOrderEntryApp::send(const std::string& msg) const {
  if (!api_transport_ || msg.empty()) {
    return false;
  }
  logger_.info("[WsOrderEntryApp] Sending message to server :{}", msg);
  return api_transport_->write(msg) >= 0;
}

void WsOrderEntryApp::register_callback(const MsgType& type,
    std::function<void(const WireMessage&)> callback) {
  callbacks_[type] = std::move(callback);
}

std::string WsOrderEntryApp::create_log_on_message(const std::string& sig_b64,
    const std::string& timestamp) const {
  return ws_oe_core_.create_log_on_message(sig_b64, timestamp);
}

std::string WsOrderEntryApp::create_log_out_message() const {
  return ws_oe_core_.create_log_out_message();
}

// NOLINTBEGIN(performance-unnecessary-value-param)
std::string WsOrderEntryApp::create_heartbeat_message(
    WireMessage /*message*/) const {
  return ws_oe_core_.create_heartbeat_message();
}
// NOLINTEND(performance-unnecessary-value-param)

std::string WsOrderEntryApp::create_order_message(
    const trading::NewSingleOrderData& order_data) const {
  return ws_oe_core_.create_order_message(order_data);
}

std::string WsOrderEntryApp::create_cancel_order_message(
    const trading::OrderCancelRequest& cancel_request) const {
  return ws_oe_core_.create_cancel_order_message(cancel_request);
}

std::string WsOrderEntryApp::create_cancel_and_reorder_message(
    const trading::OrderCancelAndNewOrderSingle& cancel_and_re_order) const {
  return ws_oe_core_.create_cancel_and_reorder_message(cancel_and_re_order);
}

std::string WsOrderEntryApp::create_order_all_cancel(
    const trading::OrderMassCancelRequest& all_order_cancel) const {
  return ws_oe_core_.create_order_all_cancel(all_order_cancel);
}

trading::ExecutionReport* WsOrderEntryApp::create_execution_report_message(
    const WireExecutionReport& msg) const {
  return ws_oe_core_.create_execution_report_message(msg);
}

trading::OrderCancelReject* WsOrderEntryApp::create_order_cancel_reject_message(
    const WireCancelReject& msg) const {
  return ws_oe_core_.create_order_cancel_reject_message(msg);
}

trading::OrderMassCancelReport*
WsOrderEntryApp::create_order_mass_cancel_report_message(
    const WireMassCancelReport& msg) const {
  return ws_oe_core_.create_order_mass_cancel_report_message(msg);
}

trading::OrderReject WsOrderEntryApp::create_reject_message(
    const WireReject& msg) const {
  return ws_oe_core_.create_reject_message(msg);
}

WsOrderEntryApp::WireMessage WsOrderEntryApp::decode(
    const std::string& message) {
  return ws_oe_core_.decode(message);
}
void WsOrderEntryApp::post_new_order(const trading::NewSingleOrderData& data) {
  PendingOrderRequest request;
  request.client_order_id = data.cl_order_id.value;
  request.symbol = data.symbol;
  request.side = trading::to_common_side(data.side);
  request.price = data.price;
  request.order_qty = data.order_qty;
  request.ord_type = data.ord_type;
  request.time_in_force = data.time_in_force;
  ws_order_manager_.register_pending_request(request);
}

void WsOrderEntryApp::post_cancel_order(
    const trading::OrderCancelRequest& data) {
  PendingOrderRequest request;
  request.client_order_id = data.cl_order_id.value;
  request.symbol = data.symbol;
  // Cancel requests have minimal info - only clientOrderId and symbol matter
  ws_order_manager_.register_pending_request(request);
}

void WsOrderEntryApp::post_cancel_and_reorder(
    const trading::OrderCancelAndNewOrderSingle& data) {
  // Track the NEW order information (not the cancel part)
  PendingOrderRequest request;
  request.client_order_id = data.cl_new_order_id.value;
  request.symbol = data.symbol;
  request.side = trading::to_common_side(data.side);
  request.price = data.price;
  request.order_qty = data.order_qty;
  request.ord_type = data.ord_type;
  request.time_in_force = data.time_in_force;
  ws_order_manager_.register_pending_request(request);
}

void WsOrderEntryApp::post_mass_cancel_order(
    const trading::OrderMassCancelRequest& data) {
  PendingOrderRequest request;
  request.client_order_id = data.cl_order_id.value;
  request.symbol = data.symbol;
  // Mass cancel has minimal info
  ws_order_manager_.register_pending_request(request);
}

void WsOrderEntryApp::create_log_on() const {
  if constexpr (WsOeCoreImpl::ExchangeTraits::requires_signature_logon()) {
    const auto cur_timestamp = std::to_string(util::get_timestamp_epoch());
    constexpr int kRecvWindow = 5000;
    const std::string sig_b64 =
        ::get_signature_base64(cur_timestamp, kRecvWindow);
    auto log_on_message =
        ws_oe_core_.create_log_on_message(sig_b64, cur_timestamp);
    api_transport_->write(log_on_message);
  } else {
    if constexpr (WsOeCoreImpl::ExchangeTraits::requires_listen_key()) {
      const std::string user_stream_msg =
          ws_oe_core_.create_user_data_stream_subscribe();
      if (UNLIKELY(!user_stream_msg.empty())) {
        api_transport_->write(user_stream_msg);
        logger_.info("[WsOeApp] Sent userDataStream.start request");
      }
    }
  }
}
void WsOrderEntryApp::handle_api_payload(std::string_view payload) {
  if (payload.empty()) {
    return;
  }
  if (payload == "__CONNECTED__") {
    create_log_on();
    return;
  }

  constexpr int kDefaultLogLen = 200;
  logger_.debug("Received payload (size: {}): {}...",
      payload.size(),
      payload.substr(0, std::min<size_t>(kDefaultLogLen, payload.size())));

  START_MEASURE(Convert_Message);
  const WireMessage message = ws_oe_core_.decode(payload);
  END_MEASURE(Convert_Message, logger_);

  if (UNLIKELY(std::holds_alternative<std::monostate>(message))) {
    return;
  }

  WsOeCoreImpl::ExchangeTraits::DispatchRouter::process_message(message,
      dispatch_context_);
}

void WsOrderEntryApp::dispatch(const std::string& type,
    const WireMessage& message) const {
  const auto callback = callbacks_.find(type);
  if (callback == callbacks_.end() || !callback->second) {
    logger_.warn("No callback registered for message type {}", type);
    return;
  }
  callback->second(message);
}
std::string WsOrderEntryApp::get_signature_base64(const std::string& payload) {
  EVP_PKEY* private_key = Util::load_ed25519(AUTHORIZATION.get_pem_file_path(),
      AUTHORIZATION.get_private_password().c_str());

  const std::string signature = Util::sign_and_base64(private_key, payload);

  Util::free_key(private_key);
  return signature;
}

void WsOrderEntryApp::handle_stream_payload(std::string_view payload) {
  if constexpr (WsOeCoreImpl::ExchangeTraits::requires_stream_transport()) {
    if (payload.empty()) {
      return;
    }

    constexpr int kDefaultLogLen = 200;
    logger_.debug("Received stream payload (size: {}): {}...",
        payload.size(),
        payload.substr(0, std::min<size_t>(kDefaultLogLen, payload.size())));

    START_MEASURE(Convert_Stream_Message);
    const WireMessage message = ws_oe_core_.decode(payload);
    END_MEASURE(Convert_Stream_Message, logger_);

    if (UNLIKELY(std::holds_alternative<std::monostate>(message))) {
      return;
    }

    WsOeCoreImpl::ExchangeTraits::DispatchRouter::process_message(message,
        dispatch_context_);
  } else {
    (void)payload;
    return;
  }
}

void WsOrderEntryApp::handle_listen_key_response(
    const std::string& listen_key) {
  start_stream_transport_impl(stream_transport_, listen_key);
}

}  // namespace core
