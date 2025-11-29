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

#include "ws_md_app.h"

#include "common/ini_config.hpp"

constexpr int kHttpOK = 200;
namespace core {

WsMarketDataApp::WsMarketDataApp(const std::string& /*sender_comp_id*/,
    const std::string& /*target_comp_id*/, common::Logger* logger,
    common::MemoryPool<MarketData>* market_data_pool)
    : logger_(logger->make_producer()),
      ws_md_core_(logger, market_data_pool),
      host_(AUTHORIZATION.get_md_ws_address()),
      path_(AUTHORIZATION.get_md_ws_path()),
      port_(AUTHORIZATION.get_md_ws_port()),
      use_ssl_(AUTHORIZATION.use_md_ws_ssl()),
      write_host_(AUTHORIZATION.get_md_ws_write_address()),
      write_path_(AUTHORIZATION.get_md_ws_write_path()),
      write_port_(AUTHORIZATION.get_md_ws_write_port()),
      write_use_ssl_(AUTHORIZATION.use_md_ws_write_ssl()) {}

WsMarketDataApp::~WsMarketDataApp() {
  stop();
}

void WsMarketDataApp::initialize_stream() {
  read_transport_ = std::make_unique<WebSocketTransport<"MDRead">>(host_,
      port_,
      path_,
      use_ssl_);

  read_transport_->register_message_callback(
      [this](std::string_view payload) { this->handle_payload(payload); });
}
bool WsMarketDataApp::start() {
  if (running_.exchange(true)) {
    return false;
  }
  write_transport_ =
      std::make_unique<WebSocketTransport<"MDWrite">>(write_host_,
          write_port_,
          write_path_,
          write_use_ssl_,
          true);

  write_transport_->register_message_callback(
      [this](std::string_view payload) { this->handle_payload(payload); });

  initialize_stream();

  return true;
}

void WsMarketDataApp::stop() {
  if (!running_.exchange(false)) {
    return;
  }
  if (read_transport_) {
    read_transport_->interrupt();
  }
  if (write_transport_) {
    write_transport_->interrupt();
  }
  read_transport_.reset();
  write_transport_.reset();
}

bool WsMarketDataApp::send(const std::string& msg) const {
  if (msg.empty() || !write_transport_) {
    return false;
  }
  logger_.info(
      std::format("[WsMarketDataApp] Sending message to api server :{}", msg));
  return write_transport_->write(msg) >= 0;
}

bool WsMarketDataApp::send_to_stream(const std::string& msg) const {
  if (msg.empty() || !read_transport_) {
    return false;
  }
  logger_.info(
      std::format("[WsMarketDataApp] Sending message to stream server :{}",
          msg));
  return read_transport_->write(msg) >= 0;
}

void WsMarketDataApp::register_callback(const MsgType& type,
    std::function<void(const WireMessage&)> callback) {
  callbacks_[type] = std::move(callback);
}

std::string WsMarketDataApp::create_log_on_message(
    const std::string& /*sig_b64*/, const std::string& /*timestamp*/) {
  return {};
}

std::string WsMarketDataApp::create_log_out_message() {
  return {};
}

std::string WsMarketDataApp::create_heartbeat_message(
    const WireMessage& /*message*/) {
  return {};
}

std::string WsMarketDataApp::create_market_data_subscription_message(
    const RequestId& request_id, const MarketDepthLevel& level,
    const SymbolId& symbol, const bool subscribe) {
  return ws_md_core_.create_market_data_subscription_message(request_id,
      level,
      symbol,
      subscribe);
}

std::string WsMarketDataApp::create_trade_data_subscription_message(
    const RequestId& request_id, const MarketDepthLevel& level,
    const SymbolId& symbol) {
  return ws_md_core_.create_trade_data_subscription_message(request_id,
      level,
      symbol);
}
MarketUpdateData WsMarketDataApp::create_market_data_message(
    const WireMessage& msg) {
  return ws_md_core_.create_market_data_message(msg);
}

MarketUpdateData WsMarketDataApp::create_snapshot_data_message(
    const WireMessage& msg) {
  return ws_md_core_.create_snapshot_data_message(msg);
}

std::string WsMarketDataApp::create_snapshot_request_message(
    const SymbolId& symbol) {
  const std::string level = INI_CONFIG.get("meta", "level");
  return ws_md_core_.create_market_data_subscription_message("snapshot",
      level,
      symbol,
      true);
}

std::string WsMarketDataApp::request_instrument_list_message(
    const std::string& symbol) const {
  return ws_md_core_.request_instrument_list_message(symbol);
}

InstrumentInfo WsMarketDataApp::create_instrument_list_message(
    const WireMessage& msg) const {
  return ws_md_core_.create_instrument_list_message(msg);
}

MarketDataReject WsMarketDataApp::create_reject_message(
    const WireMessage& msg) const {
  return ws_md_core_.create_reject_message(msg);
}

void WsMarketDataApp::handle_payload(std::string_view payload) {
  if (payload.empty()) {
    return;
  }
  if (payload == "__CONNECTED__") {
    dispatch("A", WireMessage{});
    return;
  }

  static constexpr int kMinimumLogPrintSize = 200;
  logger_.debug(std::format("Received payload (size: {}): {}...",
      payload.size(),
      payload.substr(0,
          std::min<size_t>(kMinimumLogPrintSize, payload.size()))));

  WireMessage message = ws_md_core_.decode(payload);

  std::visit(
      [this](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, schema::DepthSnapshot>) {
          handle_depth_snapshot(arg);
        } else if constexpr (std::is_same_v<T, schema::DepthResponse>) {
          handle_depth_response(arg);
        } else if constexpr (std::is_same_v<T, schema::TradeEvent>) {
          handle_trade_event(arg);
        } else if constexpr (std::is_same_v<T, schema::ExchangeInfoResponse>) {
          handle_exchange_info_response(arg);
        }
      },
      message);
}

void WsMarketDataApp::dispatch(std::string_view type,
    const WireMessage& message) const {
  const auto callback = callbacks_.find(std::string(type));
  if (callback == callbacks_.end() || !callback->second) {
    logger_.warn(
        std::format("No callback registered for message type {}", type));
    return;
  }
  callback->second(message);
}

void WsMarketDataApp::handle_depth_snapshot(
    schema::DepthSnapshot& response) const {
  if (LIKELY(response.status == kHttpOK)) {
    dispatch("W", response);
  } else {
    logger_.warn(std::format("Depth snapshot request failed with status: {}",
        response.status));
  }
}
void WsMarketDataApp::handle_depth_response(
    schema::DepthResponse& response) const {
  dispatch("X", response);
}
void WsMarketDataApp::handle_trade_event(schema::TradeEvent& response) const {
  dispatch("X", response);
}
void WsMarketDataApp::handle_exchange_info_response(
    schema::ExchangeInfoResponse& response) const {
  dispatch("y", response);
}
}  // namespace core
