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

#include "common/logger.h"
#include "common/spsc_queue.h"
#include "core/market_data.h"
#include "json_md_decoder.hpp"
#include "sbe_md_decoder.hpp"
#include "websocket/connection_handler.h"
#include "ws_md_core.h"
#include "ws_transport.h"

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
using WsMdCoreImpl = WsMdCore<BinanceFuturesTraits, JsonMdDecoder>;
using WsMdCoreApiImpl = WsMdCore<BinanceFuturesTraits, JsonMdDecoder>;
#else
using WsMdCoreImpl = WsMdCore<BinanceSpotTraits, SbeMdDecoder>;
using WsMdCoreApiImpl = WsMdCore<BinanceSpotTraits, JsonMdDecoder>;
#endif
#else

#ifdef USE_FUTURES_API
using WsMdCoreImpl = WsMdCore<BinanceFuturesTraits, JsonMdDecoder>;
using WsMdCoreApiImpl = WsMdCore<BinanceFuturesTraits, JsonMdDecoder>;
#else
using WsMdCoreImpl = WsMdCore<BinanceSpotTraits, JsonMdDecoder>;
using WsMdCoreApiImpl = WsMdCore<BinanceSpotTraits, JsonMdDecoder>;
#endif
#endif

class WsMarketDataApp {
 public:
  using ExchangeTraits = WsMdCoreImpl::ExchangeTraits;
  using WireMessage = WsMdCoreImpl::WireMessage;
  using MsgType = std::string;
  using RequestId = std::string_view;
  using MarketDepthLevel = std::string_view;
  using SymbolId = std::string_view;

  WsMarketDataApp(const std::string& sender_comp_id,
      const std::string& target_comp_id, common::Logger* logger,
      common::MemoryPool<MarketData>* market_data_pool);
  ~WsMarketDataApp();

  bool start();
  void stop();

  bool send(const std::string& msg) const;            //to api
  bool send_to_stream(const std::string& msg) const;  //to stream
  void register_callback(const MsgType& type,
      std::function<void(const WireMessage&)> callback);

  static std::string create_log_on_message(const std::string& sig_b64,
      const std::string& timestamp);
  static std::string create_log_out_message();
  static std::string create_heartbeat_message(const WireMessage& message);

  [[nodiscard]] std::string create_market_data_subscription_message(
      const RequestId& request_id, const MarketDepthLevel& level,
      const SymbolId& symbol, bool subscribe) const;
  [[nodiscard]] std::string create_trade_data_subscription_message(
      const RequestId& request_id, const MarketDepthLevel& level,
      const SymbolId& symbol, bool subscribe) const;
  [[nodiscard]] std::string create_snapshot_data_subscription_message(
      const SymbolId& symbol, const MarketDepthLevel& level) const;
  [[nodiscard]] MarketUpdateData create_market_data_message(
      const WireMessage& msg) const;
  [[nodiscard]] MarketUpdateData create_snapshot_data_message(
      const WireMessage& msg) const;
  [[nodiscard]] std::string create_snapshot_request_message(
      const SymbolId& symbol, MarketDepthLevel level) const;
  std::string request_instrument_list_message(
      const std::string& symbol = "") const;
  InstrumentInfo create_instrument_list_message(const WireMessage& msg) const;
  MarketDataReject create_reject_message(const WireMessage& msg) const;

  // Fetch instrument info via HTTP (for Futures)
  std::optional<InstrumentInfo> fetch_instrument_info_http(
      const std::string& symbol = "") const;
  void dispatch(std::string_view type, const WireMessage& message) const;

 private:
  void initialize_stream();
  void handle_stream_payload(std::string_view payload) const;
  void handle_api_payload(std::string_view payload) const;

  common::Logger::Producer logger_;
  WsMdCoreImpl stream_core_;
  WsMdCoreApiImpl api_core_;
  std::unique_ptr<WebSocketTransport<"MDRead">> stream_transport_;
  std::unique_ptr<WebSocketTransport<"MDWrite">> api_transport_;

  std::atomic<bool> running_{false};
  std::atomic<bool> snapshot_received_{false};

  std::unordered_map<MsgType, std::function<void(const WireMessage&)>>
      callbacks_;

  // Message buffering for incomplete fragments (separate buffers per transport)
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

}  // namespace core

#endif  //WS_MARKET_DATA_APP_H