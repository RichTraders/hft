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

#include "common/authorization.h"
#include "common/logger.h"
#include "common/spsc_queue.h"
#include "core/market_data.h"
#include "ws_md_core.h"
#include "ws_transport.h"

namespace core {

class WsMarketDataApp {
 public:
  using WireMessage = WsMdCore::WireMessage;
  using MsgType = std::string;
  using RequestId = std::string;
  using MarketDepthLevel = std::string;
  using SymbolId = std::string;

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
      const SymbolId& symbol, bool subscribe);
  std::string create_trade_data_subscription_message(
      const RequestId& request_id, const MarketDepthLevel& level,
      const SymbolId& symbol);
  MarketUpdateData create_market_data_message(const WireMessage& msg);
  MarketUpdateData create_snapshot_data_message(const WireMessage& msg);
  std::string create_snapshot_request_message(const SymbolId& symbol);
  std::string request_instrument_list_message(
      const std::string& symbol = "") const;
  InstrumentInfo create_instrument_list_message(const WireMessage& msg) const;
  MarketDataReject create_reject_message(const WireMessage& msg) const;

 private:
  void initialize_stream();
  void handle_payload(std::string_view payload);
  void dispatch(std::string_view type, const WireMessage& message) const;
  void handle_depth_snapshot(schema::DepthSnapshot& response) const;
  void handle_depth_response(schema::DepthResponse& response) const;
  void handle_trade_event(schema::TradeEvent& response) const;
  void handle_exchange_info_response(
      schema::ExchangeInfoResponse& response) const;

  common::Logger::Producer logger_;
  core::WsMdCore ws_md_core_;
  std::unique_ptr<core::WebSocketTransport<"MDRead">> read_transport_;
  std::unique_ptr<core::WebSocketTransport<"MDWrite">> write_transport_;

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
