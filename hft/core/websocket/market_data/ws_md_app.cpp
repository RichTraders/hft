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

#include "authorization.h"
#include "common/ini_config.hpp"
#include "performance.h"

constexpr int kHttpOK = 200;
namespace core {

WsMarketDataApp::WsMarketDataApp(const std::string& /*sender_comp_id*/,
    const std::string& /*target_comp_id*/, common::Logger* logger,
    common::MemoryPool<MarketData>* market_data_pool)
    : logger_(logger->make_producer()),
      stream_core_(logger, market_data_pool),
      api_core_(logger, market_data_pool),
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
  if constexpr (WsMdCoreImpl::PolicyType::requires_api_key()) {
    stream_transport_ = std::make_unique<WebSocketTransport<"MDRead">>(host_,
        port_,
        path_,
        use_ssl_,
        false,
        AUTHORIZATION.get_api_key());
  } else {
    stream_transport_ = std::make_unique<WebSocketTransport<"MDRead">>(host_,
        port_,
        path_,
        use_ssl_,
        false);
  }

  stream_transport_->register_message_callback(
      [this](
          std::string_view payload) { this->handle_stream_payload(payload); });
}
bool WsMarketDataApp::start() {
  if (running_.exchange(true)) {
    return false;
  }
  api_transport_ = std::make_unique<WebSocketTransport<"MDWrite">>(write_host_,
      write_port_,
      write_path_,
      write_use_ssl_,
      true);

  api_transport_->register_message_callback(
      [this](std::string_view payload) { this->handle_api_payload(payload); });

  initialize_stream();

  logger_.info("WsMarketDataApp started");
  return true;
}

void WsMarketDataApp::stop() {
  if (!running_.exchange(false)) {
    return;
  }
  if (stream_transport_) {
    stream_transport_->interrupt();
  }
  if (api_transport_) {
    api_transport_->interrupt();
  }
  stream_transport_.reset();
  api_transport_.reset();
}

bool WsMarketDataApp::send(const std::string& msg) const {
  if (msg.empty() || !api_transport_) {
    return false;
  }
  logger_.info("[WsMarketDataApp] Sending message to api server :{}", msg);
  return api_transport_->write(msg) >= 0;
}

bool WsMarketDataApp::send_to_stream(const std::string& msg) const {
  if (msg.empty() || !stream_transport_) {
    return false;
  }
  logger_.info("[WsMarketDataApp] Sending message to stream server :{}", msg);
  return stream_transport_->write(msg) >= 0;
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
    const SymbolId& symbol, const bool subscribe) const {
  return stream_core_.create_market_data_subscription_message(request_id,
      level,
      symbol,
      subscribe);
}

std::string WsMarketDataApp::create_trade_data_subscription_message(
    const RequestId& request_id, const MarketDepthLevel& level,
    const SymbolId& symbol, bool subscribe) const {
  return stream_core_.create_trade_data_subscription_message(request_id,
      level,
      symbol,
      subscribe);
}

std::string WsMarketDataApp::create_snapshot_data_subscription_message(
    const SymbolId& symbol, const MarketDepthLevel& level) const {
  return stream_core_.create_snapshot_data_subscription_message(symbol, level);
}

MarketUpdateData WsMarketDataApp::create_market_data_message(
    const WireMessage& msg) const {
  return stream_core_.create_market_data_message(msg);
}

MarketUpdateData WsMarketDataApp::create_snapshot_data_message(
    const WireMessage& msg) const {
  return stream_core_.create_snapshot_data_message(msg);
}

std::string WsMarketDataApp::create_snapshot_request_message(
    const SymbolId& symbol, MarketDepthLevel level) const {
  return stream_core_.create_snapshot_data_subscription_message(symbol, level);
}

std::string WsMarketDataApp::request_instrument_list_message(
    const std::string& symbol) const {
  return stream_core_.request_instrument_list_message(symbol);
}

InstrumentInfo WsMarketDataApp::create_instrument_list_message(
    const WireMessage& msg) const {
  return stream_core_.create_instrument_list_message(msg);
}

MarketDataReject WsMarketDataApp::create_reject_message(
    const WireMessage& msg) const {
  return stream_core_.create_reject_message(msg);
}

void WsMarketDataApp::handle_stream_payload(std::string_view payload) const {
  if (payload.empty()) {
    return;
  }
  if (payload == "__CONNECTED__") {
    dispatch("A", WireMessage{});
    return;
  }

  static constexpr int kMinimumLogPrintSize = 200;
  logger_.trace("Received stream payload (size: {}): {}...",
      payload.size(),
      payload.substr(0,
          std::min<size_t>(kMinimumLogPrintSize, payload.size())));

  START_MEASURE(Convert_Message);
  WireMessage wire_msg = stream_core_.decode(payload);
  END_MEASURE(Convert_Message, logger_);

  std::visit(
      [this, &wire_msg](const auto& arg) {
        using T = std::decay_t<decltype(arg)>;

        if constexpr (std::is_same_v<T, schema::sbe::SbeDepthSnapshot>) {
          handle_depth_snapshot(arg, wire_msg);
        } else if constexpr (std::is_same_v<T, schema::sbe::SbeDepthResponse>) {
          handle_depth_response(arg, wire_msg);
        } else if constexpr (std::is_same_v<T, schema::sbe::SbeTradeEvent>) {
          handle_trade_event(arg, wire_msg);
        } else if constexpr (std::is_same_v<T, schema::sbe::SbeBestBidAsk>) {
          handle_best_bid_ask(arg, wire_msg);
        }
      },
      wire_msg);
}

void WsMarketDataApp::handle_api_payload(std::string_view payload) const {
  if (payload.empty()) {
    return;
  }
  if (payload == "__CONNECTED__") {
    dispatch("A", WireMessage{});
    return;
  }

  static constexpr int kMinimumLogPrintSize = 200;
  logger_.trace("Received API payload (size: {}): {}...",
      payload.size(),
      payload.substr(0,
          std::min<size_t>(kMinimumLogPrintSize, payload.size())));

  START_MEASURE(Convert_Message);
  auto api_wire_msg = api_core_.decode(payload);
  END_MEASURE(Convert_Message, logger_);

  std::visit(
      [this](const auto& arg) {
        using T = std::decay_t<decltype(arg)>;

        if constexpr (std::is_same_v<T, std::monostate>) {
        } else if constexpr (std::is_same_v<T, schema::DepthSnapshot>) {
          const auto& wire_msg = arg;
          handle_depth_snapshot(arg, wire_msg);
        } else if constexpr (std::is_same_v<T, schema::ExchangeInfoResponse>) {
          const auto& wire_msg = arg;
          handle_exchange_info_response(arg, wire_msg);
        }
      },
      api_wire_msg);
}

void WsMarketDataApp::dispatch(std::string_view type,
    const WireMessage& message) const {
  const auto callback = callbacks_.find(std::string(type));
  if (callback == callbacks_.end() || !callback->second) {
    logger_.warn("No callback registered for message type {}", type);
    return;
  }
  callback->second(message);
}

void WsMarketDataApp::handle_depth_snapshot(
    const schema::DepthSnapshot& snapshot, const WireMessage& wire_msg) const {
  if (LIKELY(snapshot.status == kHttpOK)) {
    dispatch("W", wire_msg);
  } else {
    logger_.warn("Depth snapshot request failed with status: {}",
        snapshot.status);
  }
}

void WsMarketDataApp::handle_depth_response(
    const schema::DepthResponse& /*response*/,
    const WireMessage& wire_msg) const {
  dispatch("X", wire_msg);
}

void WsMarketDataApp::handle_trade_event(const schema::TradeEvent& /*trade*/,
    const WireMessage& wire_msg) const {
  dispatch("X", wire_msg);
}
void WsMarketDataApp::handle_depth_snapshot(
    const schema::sbe::SbeDepthSnapshot& /*snapshot*/,
    const WireMessage& wire_msg) const {
  dispatch("W", wire_msg);
}

void WsMarketDataApp::handle_depth_response(
    const schema::sbe::SbeDepthResponse& /*response*/,
    const WireMessage& wire_msg) const {
  dispatch("X", wire_msg);
}

void WsMarketDataApp::handle_trade_event(
    const schema::sbe::SbeTradeEvent& /*trade*/,
    const WireMessage& wire_msg) const {
  dispatch("X", wire_msg);
}

void WsMarketDataApp::handle_best_bid_ask(
    const schema::sbe::SbeBestBidAsk& /*bba*/,
    const WireMessage& wire_msg) const {
  dispatch("X", wire_msg);
}

void WsMarketDataApp::handle_exchange_info_response(
    const schema::ExchangeInfoResponse& /*info*/,
    const WireMessage& wire_msg) const {
  dispatch("y", wire_msg);
}
}  // namespace core
