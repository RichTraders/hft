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

#include "common/spsc_queue.h"
#include <common/thread.hpp>

#include "authorization.h"
#include "logger.h"

constexpr int kQueueSize = 8;
constexpr int kReadBufferSize = 1024;
constexpr int kWriteThreadSleep = 100;

namespace FIX8 {
class Message;
}

namespace core {
class SSLSocket;

template <typename Derived, FixedString ReadThreadName, FixedString WriteThreadName>
class FixApp {
public:
  FixApp(const std::string& address,
         int port,
         std::string sender_comp_id,
         std::string target_comp_id,
         common::Logger* logger,
         const Authorization& authorization);

  ~FixApp();

  using MsgType = std::string;
  using SendId = std::string;
  using TargetId = std::string;
  //DEPTH_STREAM, BOOK_TICKER_STREAM, TRADE_STREAM
  using RequestId = std::string;
  using MarketDepthLevel = std::string;
  using SymbolId = std::string;

  bool start();

  void stop();

  bool send(const std::string& msg) const;

  void write_loop();

  void read_loop();

  void register_callback(const MsgType& type,
                         const std::function<void(FIX8::Message*)>& callback);

#ifdef REPOSITORY
  void register_callback(std::function<void(const std::string&)> cb) {
    raw_data_callback_ = std::move(cb);
  }
#endif

  [[nodiscard]] std::string create_log_on(
      const std::string& sig_b64, const std::string& timestamp);

  [[nodiscard]] std::string create_log_out();

  std::string create_heartbeat(FIX8::Message* message);
  void encode(std::string& data, FIX8::Message* msg) const;

  std::string timestamp();

private:
  static bool strip_to_header(std::string& buffer);

  std::string get_signature_base64(const std::string& timestamp) const;

  static bool peek_full_message_len(const std::string& buffer, size_t& msg_len);

  static bool extract_next_message(std::string& buffer, std::string& msg);

  void process_message(const std::string& raw_msg);

  common::Logger* logger_;
  std::unique_ptr<SSLSocket> tls_sock_;
  std::map<std::string, std::function<void(FIX8::Message*)>> callbacks_;

#ifdef REPOSITORY
  std::function<void(const std::string&)> raw_data_callback_;
#endif
  std::unique_ptr<common::SPSCQueue<std::string>> queue_;

  common::Thread<WriteThreadName> write_thread_;
  common::Thread<ReadThreadName> read_thread_;
  bool thread_running_{true};

  bool log_on_{false};
  const std::string sender_id_;
  const std::string target_id_;
  const Authorization authorization_;
};
} // namespace core

#endif  //FIX_PROTOCOL_H