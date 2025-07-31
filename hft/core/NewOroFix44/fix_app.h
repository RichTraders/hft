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
#include "logger.h"
#include "market_data.h"
#include "performance.h"
#include "signature.h"
#include "ssl_socket.h"

constexpr int kQueueSize = 8;
constexpr int kReadBufferSize = 1024;
constexpr int kWriteThreadSleep = 100;

namespace FIX8::NewOroFix44OE {
class ExecutionReport;
}

namespace FIX8 {
class Message;
}

namespace core {

template <typename Derived, int Cpu = 1>
class FixApp {
public:
  FixApp(const std::string& address, int port, std::string sender_comp_id,
         std::string target_comp_id, common::Logger* logger);

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

protected:
  bool strip_to_header(std::string& buffer) {
    const size_t pos = buffer.find("8=FIX");
    if (pos == std::string::npos) {
      // 헤더가 없으면 entire buffer 가 garbage
      buffer.clear();
      return false;
    }
    if (pos > 0) {
      buffer.erase(0, pos);
    }
    return true;
  }

  const std::string get_signature_base64(const std::string& timestamp) const {
    // TODO(jb): use config reader
    EVP_PKEY* private_key = Util::load_ed25519(
        "/home/neworo2/neworo_hft/hft/resources/private.pem", "neworo");

    // payload = "A<SOH>Sender<SOH>Target<SOH>1<SOH>20250709-00:49:41.041346"
    const std::string payload = std::string("A") + SOH + sender_id_ + SOH +
                                target_id_ + SOH + "1" + SOH + timestamp;

    return Util::sign_and_base64(private_key, payload);
  }

  bool peek_full_message_len(const std::string& buffer, size_t& msg_len) const {
    constexpr size_t kBegin = 0;
    const size_t body_start = buffer.find("9=", kBegin);
    if (body_start == std::string::npos)
      return false;

    const size_t body_end = buffer.find('\x01', body_start);
    if (body_end == std::string::npos)
      return false;

    const int body_len =
        std::stoi(buffer.substr(body_start + 2, body_end - (body_start + 2)));
    const size_t header_len = (body_end + 1) - kBegin;
    msg_len = header_len + body_len +
              7; // NOLINT(readability-magic-numbers) 7 = "10=" + 3bytes + SOH
    return buffer.size() >= msg_len;
  }

  bool extract_next_message(std::string& buffer, std::string& msg) {
    if (!strip_to_header(buffer))
      return false;

    size_t msg_len = 0;
    if (!peek_full_message_len(buffer, msg_len))
      return false;

    msg = buffer.substr(0, msg_len);
    buffer.erase(0, msg_len);
    return true;
  }

  void process_message(const std::string& raw_msg);

  common::Logger* logger_;
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
} // namespace core

#endif  //FIX_PROTOCOL_H