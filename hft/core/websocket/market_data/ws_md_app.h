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
#include "decoder_policy.h"
#include "ws_md_core.h"
#include "ws_transport.h"

namespace core {
#ifdef ENABLE_SBE_DECODER
using WsMdCoreImpl = WsMdCore<SbeDecoderPolicy>;
#else
using WsMdCoreImpl = WsMdCore<JsonDecoderPolicy>;
#endif

class WsMarketDataApp {
 public:
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

 private:
  void initialize_stream();
  void handle_stream_payload(std::string_view payload) const;
  void handle_api_payload(std::string_view payload) const;
  void dispatch(std::string_view type, const WireMessage& message) const;

  // JSON message handlers
  void handle_depth_snapshot(const schema::DepthSnapshot& snapshot,
      const WireMessage& wire_msg) const;
  void handle_depth_response(const schema::DepthResponse& response,
      const WireMessage& wire_msg) const;
  void handle_trade_event(const schema::TradeEvent& trade,
      const WireMessage& wire_msg) const;

  // SBE message handlers
  void handle_depth_snapshot(const schema::sbe::SbeDepthSnapshot& snapshot,
      const WireMessage& wire_msg) const;
  void handle_depth_response(const schema::sbe::SbeDepthResponse& response,
      const WireMessage& wire_msg) const;
  void handle_trade_event(const schema::sbe::SbeTradeEvent& trade,
      const WireMessage& wire_msg) const;
  void handle_best_bid_ask(const schema::sbe::SbeBestBidAsk& bba,
      const WireMessage& wire_msg) const;

  void handle_exchange_info_response(const schema::ExchangeInfoResponse& info,
      const WireMessage& wire_msg) const;

  common::Logger::Producer logger_;
  WsMdCoreImpl stream_core_;
  WsMdCore<JsonDecoderPolicy> api_core_;
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

  const std::string write_host_;
  const std::string write_path_;
  const int write_port_;
  const bool write_use_ssl_;
};

}  // namespace core

#endif  //WS_MARKET_DATA_APP_H
