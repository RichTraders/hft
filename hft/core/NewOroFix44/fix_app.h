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

namespace FIX8 {
class Message;
}

namespace common {
template <typename T>
class SPSCQueue;
}

namespace core {
class Fix;
class SSLSocket;

template <int Cpu = 1>
class FixApp {
public:
  FixApp(const std::string& address, int port,
         const std::string& sender_comp_id,
         const std::string& target_comp_id);
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
  int send(const std::string& msg);
  void write_loop();
  void read_loop();
  void register_callback(MsgType type,
                         std::function<void(FIX8::Message*)> cb);
  void register_callback(
      std::function<void(const std::string&)> cb);

  std::string create_log_on_message(
      const std::string& sig_b64, const std::string& timestamp) const;

  std::string create_log_out_message() const;
  std::string create_heartbeat_message() const;

  std::string create_subscription_message(const RequestId& request_id,
                                          const MarketDepthLevel& level,
                                          const SymbolId& symbol) const;
  void encode(std::string& data, FIX8::Message* msg) const;

protected:
  bool strip_to_header(std::string& buffer);
  bool peek_full_message_len(const std::string& buffer, size_t& msg_len) const;
  bool extract_next_message(std::string& buffer, std::string& msg);
  void process_message(const std::string& raw_msg);

  std::unique_ptr<Fix> fix_;
  std::unique_ptr<SSLSocket> tls_sock_;
  std::map<std::string, std::function<void(FIX8::Message*)>> callbacks_;
  std::function<void(const std::string&)> raw_data_callback;
  std::unique_ptr<common::SPSCQueue<std::string>> queue_;

  common::Thread<common::AffinityTag<Cpu>> write_thread_;
  common::Thread<common::AffinityTag<Cpu>> read_thread_;
  bool thread_running{true};

  bool log_on_{false};
  const std::string sender_id_;
  const std::string target_id_;
};
}

#endif //FIX_PROTOCOL_H