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
#include "performance.h"

namespace core {

WsMarketDataApp::WsMarketDataApp(const std::string& /*sender_comp_id*/,
    const std::string& /*target_comp_id*/, common::Logger* logger,
    common::MemoryPool<MarketData>* market_data_pool)
    : logger_(logger->make_producer()),
      stream_core_(logger, market_data_pool),
      api_core_(logger, market_data_pool),
      host_(WsMdCoreImpl::ExchangeTraits::get_stream_host()),
      path_(WsMdCoreImpl::ExchangeTraits::get_stream_endpoint_path()),
      port_(WsMdCoreImpl::ExchangeTraits::get_stream_port()),
      use_ssl_(WsMdCoreImpl::ExchangeTraits::use_ssl()),
      api_host_(WsMdCoreApiImpl::ExchangeTraits::get_api_host()),
      api_path_(WsMdCoreApiImpl::ExchangeTraits::get_api_endpoint_path()),
      api_port_(WsMdCoreApiImpl::ExchangeTraits::get_api_port()),
      api_use_ssl_(WsMdCoreApiImpl::ExchangeTraits::use_ssl()) {}

WsMarketDataApp::~WsMarketDataApp() {
  stop();
}

void WsMarketDataApp::initialize_stream() {
  if constexpr (WsMdCoreImpl::Decoder::requires_api_key()) {
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
  api_transport_ = std::make_unique<WebSocketTransport<"MDWrite">>(api_host_,
      api_port_,
      api_path_,
      api_use_ssl_,
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

  START_MEASURE(Convert_Message_Stream);
  const auto wire_msg = stream_core_.decode(payload);
  END_MEASURE(Convert_Message_Stream, logger_);

  using StreamRouter = WsMdCoreImpl::ExchangeTraits::DispatchRouter;
  StreamRouter::template process_message<WsMdCoreImpl::ExchangeTraits>(wire_msg,
      [this, &wire_msg](std::string_view type) { dispatch(type, wire_msg); });
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

  START_MEASURE(Convert_Message_API);
  auto api_wire_msg = api_core_.decode(payload);
  END_MEASURE(Convert_Message_API, logger_);

  using ApiRouter = WsMdCoreApiImpl::ExchangeTraits::DispatchRouter;
  ApiRouter::template process_message<WsMdCoreApiImpl::ExchangeTraits>(
      api_wire_msg,
      [this, &api_wire_msg](
          std::string_view type) { dispatch(type, api_wire_msg); });
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

}  // namespace core
