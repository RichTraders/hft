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
#include "schema/response/account_position.h"

constexpr int kHttpOK = 200;
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
      host_(AUTHORIZATION.get_oe_ws_address()),
      path_(AUTHORIZATION.get_oe_ws_path()),
      port_(AUTHORIZATION.get_oe_ws_port()),
      use_ssl_(AUTHORIZATION.use_oe_ws_ssl()) {}

WsOrderEntryApp::~WsOrderEntryApp() {
  stop();
}

bool WsOrderEntryApp::start() {
  if (running_.exchange(true)) {
    return false;
  }
  transport_ = std::make_unique<WebSocketTransport<"OERead">>(host_,
      port_,
      path_,
      use_ssl_,
      true);

  transport_->register_message_callback(
      [this](std::string_view payload) { this->handle_payload(payload); });
  return true;
}

void WsOrderEntryApp::stop() {
  if (!running_.exchange(false)) {
    return;
  }
  if (transport_) {
    transport_->interrupt();
  }
  transport_.reset();
}

bool WsOrderEntryApp::send(const std::string& msg) const {
  if (!transport_ || msg.empty()) {
    return false;
  }
  logger_.info("[WsOrderEntryApp] Sending message to server :{}", msg);
  return transport_->write(msg) >= 0;
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
  const auto cur_timestamp = std::to_string(util::get_timestamp_epoch());
  constexpr int kRecvWindow = 5000;
  const std::string sig_b64 =
      ::get_signature_base64(cur_timestamp, kRecvWindow);

  auto log_on_message =
      ws_oe_core_.create_log_on_message(sig_b64, cur_timestamp);
  transport_->write(log_on_message);
}
void WsOrderEntryApp::handle_payload(std::string_view payload) {
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
  WireMessage message = ws_oe_core_.decode(payload);
  END_MEASURE(Convert_Message, logger_);

  if (UNLIKELY(std::holds_alternative<std::monostate>(message))) {
    return;
  }

  std::visit(
      [this](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, schema::ExecutionReportResponse>) {
          handle_execution_report(arg);
        } else if constexpr (std::is_same_v<T,
                                 schema::OutboundAccountPositionEnvelope>) {
          handle_account_updated(arg);
        } else if constexpr (std::is_same_v<T, schema::BalanceUpdateEnvelope>) {
          handle_balance_update(arg);
        } else if constexpr (std::is_same_v<T, schema::SessionLogonResponse>) {
          handle_session_logon(arg);
        } else if constexpr (std::is_same_v<T,
                                 schema::SessionUserSubscriptionResponse>) {
          handle_user_subscription(arg);
        } else if constexpr (std::is_same_v<T, schema::ApiResponse>) {
          handle_api_response(arg);
        } else if constexpr (std::is_same_v<T,
                                 schema::CancelAndReorderResponse>) {
          handle_cancel_and_reorder_response(arg);
        } else if constexpr (std::is_same_v<T,
                                 schema::CancelAllOrdersResponse>) {
          handle_cancel_all_response(arg);
        } else if constexpr (std::is_same_v<T, schema::PlaceOrderResponse>) {
          handle_place_order_response(arg);
        }
      },
      message);
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

void WsOrderEntryApp::handle_execution_report(
    const schema::ExecutionReportResponse& ptr) {
  const auto& event = ptr.event;
  const WireMessage message = ptr;

  if (event.execution_type == "CANCELED") {
    if (event.reject_reason != "NONE") {
      dispatch("9", message);  // Cancel reject
    } else {
      dispatch("8", message);  // Regular execution report (cancel success)
    }
  } else {
    // Handle all execution reports including REJECTED, NEW, TRADE, etc.
    dispatch("8", message);  // Regular execution report
  }
  ws_order_manager_.remove_pending_request(ptr.event.client_order_id);
}

void WsOrderEntryApp::handle_balance_update(
    const schema::BalanceUpdateEnvelope& ptr) const {
  std::ostringstream stream;
  stream << "BalanceUpdated : " << ptr.event;
  logger_.debug(stream.str());
}

void WsOrderEntryApp::handle_account_updated(
    const schema::OutboundAccountPositionEnvelope& ptr) const {
  std::ostringstream stream;
  stream << "AccountUpdated : " << ptr.event;
  logger_.debug(stream.str());
}

void WsOrderEntryApp::handle_session_logon(
    const schema::SessionLogonResponse& ptr) const {
  if (ptr.status == kHttpOK) {
    logger_.info("[WsOeApp] session.logon successful");
    // Automatically start user data stream after successful login
    const std::string user_stream_msg =
        ws_oe_core_.create_user_data_stream_subscribe();
    if (UNLIKELY(!user_stream_msg.empty())) {
      send(user_stream_msg);
    }
  } else {
    if (ptr.error.has_value())
      logger_.error("[WsOeApp] session.logon failed: status={}, error={}",
          ptr.status,
          ptr.error.value().message);
  }

  const WireMessage message = ptr;
  dispatch("A", message);
}

void WsOrderEntryApp::handle_user_subscription(
    const schema::SessionUserSubscriptionResponse& ptr) {
  if (ptr.status != kHttpOK) {
    logger_.warn("[WsOeApp] UserDataStream response failed: id={}, status={}",
        ptr.id,
        ptr.status);
  }

  // No need to notify user session subscription
  /*const WireMessage message = ptr;
  dispatch("sub", message);*/
}

void WsOrderEntryApp::handle_api_response(const schema::ApiResponse& ptr) {
  if (ptr.status != kHttpOK) {
    if (ptr.error.has_value()) {
      logger_.warn("[WsOeApp] API response failed: id={}, status={}, error={}",
          ptr.id,
          ptr.status,
          ptr.error.value().message);

      const WireMessage message = ptr;
      dispatch("8", message);
    }
  }
}
void WsOrderEntryApp::handle_cancel_and_reorder_response(
    const schema::CancelAndReorderResponse& ptr) {
  if (ptr.status != kHttpOK && ptr.error.has_value()) {
    logger_.warn(
        "[WsOeApp] CancelAndReorder failed: id={}, status={}, error={}",
        ptr.id,
        ptr.status,
        ptr.error.value().message);

    auto synthetic_report =
        ws_order_manager_.create_synthetic_execution_report(ptr.id,
            ptr.error.value().code,
            ptr.error.value().message);
    if (synthetic_report.has_value()) {
      const WireMessage message = synthetic_report.value();
      dispatch("8", message);
    }
  }
}

void WsOrderEntryApp::handle_cancel_all_response(
    const schema::CancelAllOrdersResponse& ptr) {
  if (ptr.status != kHttpOK && ptr.error.has_value()) {
    logger_.warn("[WsOeApp] CancelAll failed: id={}, status={}, error={}",
        ptr.id,
        ptr.status,
        ptr.error.value().message);

    auto synthetic_report =
        ws_order_manager_.create_synthetic_execution_report(ptr.id,
            ptr.error.value().code,
            ptr.error.value().message);
    if (synthetic_report.has_value()) {
      const WireMessage message = synthetic_report.value();
      dispatch("8", message);
    }
  }
}

void WsOrderEntryApp::handle_place_order_response(
    const schema::PlaceOrderResponse& ptr) {
  if (ptr.status != kHttpOK && ptr.error.has_value()) {
    logger_.warn("[WsOeApp] PlaceOrder failed: id={}, status={}, error={}",
        ptr.id,
        ptr.status,
        ptr.error.value().message);

    auto synthetic_report =
        ws_order_manager_.create_synthetic_execution_report(ptr.id,
            ptr.error.value().code,
            ptr.error.value().message);
    if (synthetic_report.has_value()) {
      const WireMessage message = synthetic_report.value();
      dispatch("8", message);
    }
  }
}
}  // namespace core
