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

#ifndef WS_MARKET_DATA_APP_H
#define WS_MARKET_DATA_APP_H

#include "authorization.h"
#include "common/logger.h"
#include "common/spsc_queue.h"
#include "core/market_data.h"
#include "exchanges/binance/futures/binance_futures_exchange_info_fetcher.h"
#include "performance.h"
#include "sbe_md_decoder.hpp"
#include "websocket/connection_handler.h"
#include "ws_md_core.h"
#include "ws_transport.h"

#ifdef USE_RING_BUFFER
#include "common/market_data_ring_buffer.hpp"
#endif

#ifdef USE_FUTURES_API
#include "binance_futures_traits.h"
#else
#include "binance_spot_traits.h"
#endif

namespace core {

#ifdef ENABLE_SBE_DECODER
#ifdef USE_FUTURES_API
static_assert(BinanceFuturesTraits::supports_sbe(),
    "SBE is not supported for Binance Futures.");
using WsMdCoreImpl =
    WsMdCore<BinanceFuturesTraits, BinanceFuturesTraits::Decoder>;
using WsMdCoreApiImpl =
    WsMdCore<BinanceFuturesTraits, BinanceFuturesTraits::Decoder>;
#else
using WsMdCoreImpl =
    WsMdCore<BinanceSpotTraits, SbeMdDecoder<BinanceSpotTraits>>;
using WsMdCoreApiImpl = WsMdCore<BinanceSpotTraits, BinanceSpotTraits::Decoder>;
#endif
#else

#ifdef USE_FUTURES_API
using WsMdCoreImpl =
    WsMdCore<BinanceFuturesTraits, BinanceFuturesTraits::Decoder>;
using WsMdCoreApiImpl =
    WsMdCore<BinanceFuturesTraits, BinanceFuturesTraits::Decoder>;
#else
using WsMdCoreImpl = WsMdCore<BinanceSpotTraits, BinanceSpotTraits::Decoder>;
using WsMdCoreApiImpl = WsMdCore<BinanceSpotTraits, BinanceSpotTraits::Decoder>;
#endif
#endif

using DefaultStreamTransport = WebSocketTransport<"MDRead">;
using DefaultApiTransport = WebSocketTransport<"MDWrite">;

template <typename StreamTransportT = DefaultStreamTransport,
    typename ApiTransportT = DefaultApiTransport>
class WsMarketDataAppT {
 public:
  using StreamTransportType = StreamTransportT;
  using ApiTransportType = ApiTransportT;
  using ExchangeTraits = WsMdCoreImpl::ExchangeTraits;
  using WireMessage = WsMdCoreImpl::WireMessage;
  using MsgType = std::string;
  using RequestId = std::string_view;
  using MarketDepthLevel = std::string_view;
  using SymbolId = std::string_view;

  WsMarketDataAppT(const std::string& /*sender_comp_id*/,
      const std::string& /*target_comp_id*/,
      const common::Logger::Producer& logger,
      common::MemoryPool<MarketData>* market_data_pool)
      : logger_(logger),
        stream_core_(logger_, market_data_pool),
        api_core_(logger_, market_data_pool),
        host_(WsMdCoreImpl::ExchangeTraits::get_stream_host()),
        path_(WsMdCoreImpl::ExchangeTraits::get_stream_endpoint_path()),
        port_(WsMdCoreImpl::ExchangeTraits::get_stream_port()),
        use_ssl_(WsMdCoreImpl::ExchangeTraits::use_ssl()),
        api_host_(WsMdCoreApiImpl::ExchangeTraits::get_api_host()),
        api_path_(WsMdCoreApiImpl::ExchangeTraits::get_api_endpoint_path()),
        api_port_(WsMdCoreApiImpl::ExchangeTraits::get_api_port()),
        api_use_ssl_(WsMdCoreApiImpl::ExchangeTraits::use_ssl()) {}

  ~WsMarketDataAppT() { stop(); }

  bool start() {
    if (running_.exchange(true)) {
      return false;
    }
    api_transport_ = std::make_unique<ApiTransportType>(api_host_,
        api_port_,
        api_path_,
        api_use_ssl_,
        true);

    api_transport_->register_message_callback([this](std::string_view payload) {
      this->handle_api_payload(payload);
    });

    initialize_stream();

    logger_.info("WsMarketDataApp started");
    return true;
  }

  void stop() {
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

  [[nodiscard]] bool send(const std::string& msg) const {
    if (msg.empty() || !api_transport_) {
      return false;
    }
    logger_.info("[WsMarketDataApp] Sending message to api server :{}", msg);
    return api_transport_->write(msg) >= 0;
  }

  [[nodiscard]] bool send_to_stream(const std::string& msg) const {
    if (msg.empty() || !stream_transport_) {
      return false;
    }
    logger_.info("[WsMarketDataApp] Sending message to stream server :{}", msg);
    return stream_transport_->write(msg) >= 0;
  }

  void register_callback(const MsgType& type,
      std::function<void(const WireMessage&)> callback) {
    callbacks_[type] = std::move(callback);
  }

#ifdef REPOSITORY
  void register_callback(std::function<void(const std::string&,
          const WireMessage&, const std::string&)>
          cb) {
    raw_data_callback_ = std::move(cb);
  }
#endif

  static std::string create_log_on_message(const std::string& /*sig_b64*/,
      const std::string& /*timestamp*/) {
    return {};
  }

  static std::string create_log_out_message() { return {}; }

  static std::string create_heartbeat_message(const WireMessage& /*message*/) {
    return {};
  }

  [[nodiscard]] std::string create_market_data_subscription_message(
      const RequestId& request_id, const MarketDepthLevel& level,
      const SymbolId& symbol, bool subscribe) const {
    return stream_core_.create_market_data_subscription_message(request_id,
        level,
        symbol,
        subscribe);
  }

  [[nodiscard]] std::string create_trade_data_subscription_message(
      const RequestId& request_id, const MarketDepthLevel& level,
      const SymbolId& symbol, bool subscribe) const {
    return stream_core_.create_trade_data_subscription_message(request_id,
        level,
        symbol,
        subscribe);
  }

  [[nodiscard]] std::string create_snapshot_data_subscription_message(
      const SymbolId& symbol, const MarketDepthLevel& level) const {
    return stream_core_.create_snapshot_data_subscription_message(symbol,
        level);
  }

  [[nodiscard]] MarketUpdateData create_market_data_message(
      const WireMessage& msg) const {
    return stream_core_.create_market_data_message(msg);
  }

  [[nodiscard]] MarketUpdateData create_snapshot_data_message(
      const WireMessage& msg) const {
    return stream_core_.create_snapshot_data_message(msg);
  }

  [[nodiscard]] std::string create_snapshot_request_message(
      const SymbolId& symbol, MarketDepthLevel level) const {
    return stream_core_.create_snapshot_data_subscription_message(symbol,
        level);
  }

  [[nodiscard]] std::string request_instrument_list_message(
      const std::string& symbol = "") const {
    return stream_core_.request_instrument_list_message(symbol);
  }

  [[nodiscard]] InstrumentInfo create_instrument_list_message(
      const WireMessage& msg) const {
    return stream_core_.create_instrument_list_message(msg);
  }

  [[nodiscard]] MarketDataReject create_reject_message(
      const WireMessage& msg) const {
    return stream_core_.create_reject_message(msg);
  }

#ifdef USE_RING_BUFFER
  bool write_to_ring_buffer(const WireMessage& msg,
      common::MarketDataRingBuffer* ring_buffer) const {
    return stream_core_.write_to_ring_buffer(msg, ring_buffer);
  }

  bool write_snapshot_to_ring_buffer(const WireMessage& msg,
      common::MarketDataRingBuffer* ring_buffer) const {
    return stream_core_.write_snapshot_to_ring_buffer(msg, ring_buffer);
  }
#endif

  [[nodiscard]] std::optional<InstrumentInfo> fetch_instrument_info_http(
      const std::string& symbol = "") const {
    if constexpr (ExchangeTraits::uses_http_exchange_info()) {
      http::BinanceFuturesExchangeInfoFetcher fetcher(logger_);
      return fetcher.fetch(symbol);
    } else {
      return std::nullopt;
    }
  }

  void dispatch(std::string_view type, const WireMessage& message) const {
    const auto callback = callbacks_.find(std::string(type));
    if (callback == callbacks_.end() || !callback->second) {
#ifndef REPOSITORY
      logger_.warn("No callback registered for message type {}", type);
#endif
      return;
    }
    callback->second(message);
  }

  StreamTransportType& stream_transport() { return *stream_transport_; }
  ApiTransportType& api_transport() { return *api_transport_; }
  const StreamTransportType& stream_transport() const {
    return *stream_transport_;
  }
  const ApiTransportType& api_transport() const { return *api_transport_; }

 private:
  void initialize_stream() {
    // NOLINTBEGIN(bugprone-branch-clone)
    if constexpr (WsMdCoreImpl::Decoder::requires_api_key()) {
      stream_transport_ = std::make_unique<StreamTransportType>(host_,
          port_,
          path_,
          use_ssl_,
          false,
          AUTHORIZATION.get_api_key());
    } else {
      stream_transport_ = std::make_unique<StreamTransportType>(host_,
          port_,
          path_,
          use_ssl_,
          false);
    }
    // NOLINTEND(bugprone-branch-clone)

    stream_transport_->register_message_callback(
        [this](std::string_view payload) {
          this->handle_stream_payload(payload);
        });
  }

  void handle_stream_payload(std::string_view payload) const {
    if (payload.empty()) {
      return;
    }
    if (payload == "__CONNECTED__") {
      using ConnHandler =
          typename WsMdCoreImpl::ExchangeTraits::ConnectionHandler;
      ConnectionContext ctx(const_cast<WsMarketDataAppT*>(this),
          TransportId::kStream);
      ConnHandler::on_connected(ctx, TransportId::kStream);
      return;
    }

    static constexpr int kMinimumLogPrintSize = 200;
    logger_.trace("[WsMarketDataApp]Received stream payload (size: {}): {}...",
        payload.size(),
        payload.substr(0,
            std::min<size_t>(kMinimumLogPrintSize, payload.size())));

    START_MEASURE(Convert_Message_Stream);
    const auto wire_msg = stream_core_.decode(payload);
    END_MEASURE(Convert_Message_Stream, logger_);

    using StreamRouter = typename WsMdCoreImpl::ExchangeTraits::DispatchRouter;
#ifdef REPOSITORY
    StreamRouter::template process_message<
        typename WsMdCoreImpl::ExchangeTraits>(wire_msg,
        [this, &wire_msg, &payload](std::string_view type) {
          dispatch(type, wire_msg);
          if (raw_data_callback_) {
            raw_data_callback_(std::string(payload),
                wire_msg,
                std::string(type));
          }
        });
#else
    StreamRouter::template process_message<
        typename WsMdCoreImpl::ExchangeTraits>(wire_msg,
        [this, &wire_msg](std::string_view type) { dispatch(type, wire_msg); });
#endif
  }

  void handle_api_payload(std::string_view payload) const {
    if (payload.empty()) {
      return;
    }
    if (payload == "__CONNECTED__") {
      using ConnHandler =
          typename WsMdCoreApiImpl::ExchangeTraits::ConnectionHandler;
      ConnectionContext ctx(const_cast<WsMarketDataAppT*>(this),
          TransportId::kApi);
      ConnHandler::on_connected(ctx, TransportId::kApi);
      return;
    }

    static constexpr int kMinimumLogPrintSize = 200;
    logger_.info("[WsMarketDataApp]Received API payload (size: {}): {}...",
        payload.size(),
        payload.substr(0,
            std::min<size_t>(kMinimumLogPrintSize, payload.size())));

    START_MEASURE(Convert_Message_API);
    auto api_wire_msg = api_core_.decode(payload);
    END_MEASURE(Convert_Message_API, logger_);

    using ApiRouter = typename WsMdCoreApiImpl::ExchangeTraits::DispatchRouter;
#ifdef REPOSITORY
    ApiRouter::template process_message<
        typename WsMdCoreApiImpl::ExchangeTraits>(api_wire_msg,
        [this, &api_wire_msg, &payload](std::string_view type) {
          dispatch(type, api_wire_msg);
          if (raw_data_callback_) {
            raw_data_callback_(std::string(payload),
                api_wire_msg,
                std::string(type));
          }
        });
#else
    ApiRouter::template process_message<
        typename WsMdCoreApiImpl::ExchangeTraits>(api_wire_msg,
        [this, &api_wire_msg](
            std::string_view type) { dispatch(type, api_wire_msg); });
#endif
  }

  const common::Logger::Producer& logger_;
  WsMdCoreImpl stream_core_;
  WsMdCoreApiImpl api_core_;
  std::unique_ptr<StreamTransportType> stream_transport_;
  std::unique_ptr<ApiTransportType> api_transport_;

  std::atomic<bool> running_{false};
  std::atomic<bool> snapshot_received_{false};

  std::unordered_map<MsgType, std::function<void(const WireMessage&)>>
      callbacks_;

#ifdef REPOSITORY
  std::function<void(const std::string&, const WireMessage&,
      const std::string&)>
      raw_data_callback_;
#endif

  std::string read_buffer_;
  std::string write_buffer_;

  const std::string host_;
  const std::string path_;
  const int port_;
  const bool use_ssl_;

  const std::string api_host_;
  const std::string api_path_;
  const int api_port_;
  const bool api_use_ssl_;
};

using WsMarketDataApp = WsMarketDataAppT<>;

}  // namespace core

#endif  //WS_MARKET_DATA_APP_H
