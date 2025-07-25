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

#ifndef FIX_PROTOCOL_H
#define FIX_PROTOCOL_H

#include <common/thread.hpp>

#include "logger.h"
#include "market_data.h"

namespace FIX8 {
class Message;
}

namespace common {
template <typename T>
class SPSCQueue;
template <typename T>
class MemoryPool;
}

namespace core {
class Fix;
class SSLSocket;

template <int Cpu = 1>
class FixApp {
public:
  FixApp(const std::string& address, int port,
         const std::string& sender_comp_id,
         const std::string& target_comp_id, common::Logger* logger,
         common::MemoryPool<MarketData>* market_data_pool);
  ~FixApp();

  using MsgType = std::string;
  using SendId = std::string;
  using TargetId = std::string;
  //DEPTH_STREAM, BOOK_TICKER_STREAM, TRADE_STREAM
  using RequestId = std::string;
  using MarketDepthLevel = std::string;
  using SymbolId = std::string;

  int start();
  int stop();
  bool send(const std::string& msg) const;
  void write_loop();
  void read_loop();

  void register_callback(const MsgType& type,
                         const std::function<void(FIX8::Message*)>& callback);
#ifdef REPOSITORY
  void register_callback(
      std::function<void(const std::string&)> cb);
#endif

  [[nodiscard]] std::string create_log_on_message(
      const std::string& sig_b64, const std::string& timestamp) const;

  [[nodiscard]] std::string create_log_out_message() const;
  std::string create_heartbeat_message(FIX8::Message* message) const;

  [[nodiscard]] std::string create_subscription_message(const RequestId& request_id,
                                          const MarketDepthLevel& level,
                                          const SymbolId& symbol) const;
  void encode(std::string& data, FIX8::Message* msg) const;
  MarketUpdateData create_market_data_message(FIX8::Message* msg) const;
  MarketUpdateData create_snapshot_data_message(FIX8::Message* msg) const;

private:
  bool strip_to_header(std::string& buffer);
  bool peek_full_message_len(const std::string& buffer, size_t& msg_len) const;
  bool extract_next_message(std::string& buffer, std::string& msg);
  void process_message(const std::string& raw_msg);

  common::MemoryPool<MarketData>* market_data_pool_;
  common::Logger* logger_;
  std::unique_ptr<Fix> fix_;
  std::unique_ptr<SSLSocket> tls_sock_;
  std::map<std::string, std::function<void(FIX8::Message*)>> callbacks_;

#ifdef REPOSITORY
  std::function<void(const std::string&)> raw_data_callback_;
#endif
  std::unique_ptr<common::SPSCQueue<std::string>> queue_;

  common::Thread<common::AffinityTag<Cpu>> write_thread_;
  common::Thread<common::AffinityTag<Cpu>> read_thread_;
  bool thread_running_{true};

  bool log_on_{false};
  const std::string sender_id_;
  const std::string target_id_;
};
}

#endif //FIX_PROTOCOL_H