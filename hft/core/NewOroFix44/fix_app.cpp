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

#include "fix_app.h"
#include <fix8/f8includes.hpp>
#include "common/spsc_queue.h"
#include "fix_md_core.h"
#include "ssl_socket.h"

#include "performance.h"

constexpr int kQueueSize = 8;
constexpr int kReadBufferSize = 1024;
constexpr int kWriteThreadSleep = 100;

using namespace common;

namespace core {
template <int Cpu>
FixApp<Cpu>::FixApp(const std::string& address, const int port,
                    const std::string& sender_comp_id,
                    const std::string& target_comp_id, Logger* logger,
                    MemoryPool<MarketData>* market_data_pool)
    : market_data_pool_(market_data_pool),
      logger_(logger),
      fix_(std::make_unique<FixMdCore>(sender_comp_id, target_comp_id, logger,
                                 market_data_pool)),
      tls_sock_(std::make_unique<SSLSocket>(address, port)),
      queue_(std::make_unique<common::SPSCQueue<std::string>>(kQueueSize)),
      sender_id_(sender_comp_id),
      target_id_(target_comp_id) {

  write_thread_.start(&FixApp::write_loop, this);
  read_thread_.start(&FixApp::read_loop, this);
}

template <int Cpu>
FixApp<Cpu>::~FixApp() {
  const auto msg = fix_->create_log_out_message();
  tls_sock_->write(msg.data(), static_cast<int>(msg.size()));

  thread_running_ = false;
}

template <int Cpu>
int FixApp<Cpu>::start() {
  const auto timestamp = fix_->timestamp();
  const std::string sig_b64 = fix_->get_signature_base64(timestamp);

  const std::string fixmsg = fix_->create_log_on_message(sig_b64, timestamp);

  send(fixmsg);
  std::cout <<"log on sent\n";
  return 0;
}

template <int Cpu>
int FixApp<Cpu>::stop() {
  const auto msg = fix_->create_log_out_message();
  tls_sock_->write(msg.data(), static_cast<int>(msg.size()));
  thread_running_ = false;
  return 0;
}

template <int Cpu>
bool FixApp<Cpu>::send(const std::string& msg) const {
  return queue_->enqueue(msg);
}

template <int Cpu>
void FixApp<Cpu>::write_loop() {
  while (thread_running_) {
    std::string msg;

    while (queue_->dequeue(msg)) {
#ifdef DEBUG
      START_MEASURE(TLS_WRITE);
#endif
      auto result = tls_sock_->write(msg.data(), static_cast<int>(msg.size()));
      if (result < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          queue_->enqueue(msg);
          break;
        }
        perror("send failed");
        thread_running_ = false;
        break;
      }
#ifdef DEBUG
      END_MEASURE(TLS_WRITE, logger_);
#endif
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kWriteThreadSleep));
  }
}

template <int Cpu>
std::string FixApp<Cpu>::create_log_on_message(
    const std::string& sig_b64, const std::string& timestamp) const {
  return fix_->create_log_on_message(sig_b64, timestamp);
}

template <int Cpu>
std::string FixApp<Cpu>::create_log_out_message() const {
  return fix_->create_log_out_message();
}

template <int Cpu>
std::string FixApp<Cpu>::create_heartbeat_message(
    FIX8::Message* message) const {
  return fix_->create_heartbeat_message(message);
}

template <int Cpu>
std::string FixApp<Cpu>::create_subscription_message(
    const RequestId& request_id, const MarketDepthLevel& level,
    const SymbolId& symbol) const {
  return fix_->create_market_data_subscription_message(request_id, level,
                                                       symbol);
}

template <int Cpu>
void FixApp<Cpu>::encode(std::string& data, FIX8::Message* msg) const {
  fix_->encode(data, msg);
}

template <int Cpu>
MarketUpdateData FixApp<Cpu>::create_market_data_message(
    FIX8::Message* msg) const {
  return fix_->create_market_data(msg);
}

template <int Cpu>
MarketUpdateData FixApp<Cpu>::create_snapshot_data_message(
    FIX8::Message* msg) const {
  return fix_->create_snapshot_data_message(msg);
}

template <int Cpu>
bool FixApp<Cpu>::strip_to_header(std::string& buffer) {
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

template <int Cpu>
bool FixApp<Cpu>::peek_full_message_len(const std::string& buffer,
                                        size_t& msg_len) const {
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
            7;  // NOLINT(readability-magic-numbers) 7 = "10=" + 3bytes + SOH
  return buffer.size() >= msg_len;
}

template <int Cpu>
bool FixApp<Cpu>::extract_next_message(std::string& buffer, std::string& msg) {
  if (!strip_to_header(buffer))
    return false;

  size_t msg_len = 0;
  if (!peek_full_message_len(buffer, msg_len))
    return false;

  msg = buffer.substr(0, msg_len);
  buffer.erase(0, msg_len);
  return true;
}

template <int Cpu>
void FixApp<Cpu>::process_message(const std::string& raw_msg) {
  auto* msg = fix_->decode(raw_msg);
  const auto type = msg->get_msgtype();

  if (callbacks_.contains(type)) {
    callbacks_[type](msg);
  }
#ifdef REPOSITORY
  if (raw_data_callback_) {
    raw_data_callback_(raw_msg);
  }
#endif
  delete msg;
}

template <int Cpu>
void FixApp<Cpu>::read_loop() {
  std::string received_buffer;
  while (thread_running_) {
#ifdef DEBUG
    START_MEASURE(TLS_READ);
#endif
    std::array<char, kReadBufferSize> buf;
    const int read = tls_sock_->read(buf.data(), buf.size());
#ifdef DEBUG
    END_MEASURE(TLS_READ, logger_);
#endif
    if (read <= 0) {
      std::this_thread::yield();
      continue;
    }
    received_buffer.append(buf.data(), read);

    std::string raw_msg;
    while (extract_next_message(received_buffer, raw_msg)) {
      process_message(raw_msg);
    }
  }
}

template <int Cpu>
void FixApp<Cpu>::register_callback(
    const MsgType& type, const std::function<void(FIX8::Message*)>& callback) {
  if (!callbacks_.contains(type)) {
    callbacks_[type] = callback;
  } else {
    std::cout << "already registered type" << type << "\n";
  }
}

#ifdef REPOSITORY
template <int Cpu>
void FixApp<Cpu>::register_callback(
    std::function<void(const std::string&)> cb) {
  raw_data_callback_ = std::move(cb);
}
#endif

template class FixApp<1>;
template class FixApp<2>;
template class FixApp<3>;
}  // namespace core