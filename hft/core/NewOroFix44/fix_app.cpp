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

#include <fix8/f8includes.hpp>

#include "fix_oe_app.h"
#include "fix_md_app.h"
#include "performance.h"
#include "signature.h"
#include "ssl_socket.h"

namespace core {

template <typename Derived, FixedString ReadThreadName, FixedString WriteThreadName>
FixApp<Derived, ReadThreadName, WriteThreadName>::FixApp(const std::string& address,
                             int port,
                             std::string sender_comp_id,
                             std::string target_comp_id,
                             common::Logger* logger,
                             const Authorization& authorization)
  : logger_(logger),
    tls_sock_(std::make_unique<SSLSocket>(address, port)),
    queue_(std::make_unique<common::SPSCQueue<std::string>>(kQueueSize)),
    sender_id_(std::move(sender_comp_id)),
    target_id_(std::move(target_comp_id)),
    authorization_(authorization) {
  write_thread_.start(&FixApp::write_loop, this);
  read_thread_.start(&FixApp::read_loop, this);
}

template <typename Derived, FixedString ReadThreadName, FixedString WriteThreadName>
FixApp<Derived, ReadThreadName, WriteThreadName>::~FixApp() {
  thread_running_ = false;
  write_thread_.join();
  read_thread_.join();
  logger_->info("Fix write thread finish");
  logger_->info("Fix read thread finish");
}

template <typename Derived, FixedString ReadThreadName, FixedString WriteThreadName>
int FixApp<Derived, ReadThreadName, WriteThreadName>::start() {
  const std::string cur_timestamp = timestamp();
  const std::string sig_b64 = get_signature_base64(cur_timestamp);

  const std::string fixmsg = create_log_on(sig_b64, cur_timestamp);

  send(fixmsg);
  logger_->info("log on sent");
  return 0;
}

template <typename Derived, FixedString ReadThreadName, FixedString WriteThreadName>
void FixApp<Derived, ReadThreadName, WriteThreadName>::stop() {
  auto msg = static_cast<Derived*>(this)->create_log_out_message();
  send(msg);
}

template <typename Derived, FixedString ReadThreadName, FixedString WriteThreadName>
bool FixApp<Derived, ReadThreadName, WriteThreadName>::send(const std::string& msg) const {
  return queue_->enqueue(msg);
}

template <typename Derived, FixedString ReadThreadName, FixedString WriteThreadName>
void FixApp<Derived, ReadThreadName, WriteThreadName>::write_loop() {
  while (thread_running_) {
    std::string msg;

    while (queue_->dequeue(msg)) {
      START_MEASURE(TLS_WRITE);
      auto result =
          tls_sock_->write(msg.data(), static_cast<int>(msg.size()));
      if (result < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          queue_->enqueue(msg);
          break;
        }
        perror("send failed");
        thread_running_ = false;
        break;
      }
      END_MEASURE(TLS_WRITE, logger_);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kWriteThreadSleep));
  }
}

template <typename Derived, FixedString ReadThreadName, FixedString WriteThreadName>
void FixApp<Derived, ReadThreadName, WriteThreadName>::read_loop() {
  std::string received_buffer;
  while (thread_running_) {
    START_MEASURE(TLS_READ);
    std::array<char, kReadBufferSize> buf;
    const int read = tls_sock_->read(buf.data(), buf.size());
    END_MEASURE(TLS_READ, logger_);
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

template <typename Derived, FixedString ReadThreadName, FixedString WriteThreadName>
void FixApp<Derived, ReadThreadName, WriteThreadName>::register_callback(const MsgType& type,
                                             const std::function<void(
                                                 FIX8::Message*)>& callback) {
  if (!callbacks_.contains(type)) {
    callbacks_[type] = callback;
  } else {
    std::cout << "already registered type" << type << "\n";
  }
}

template <typename Derived, FixedString ReadThreadName, FixedString WriteThreadName>
[[nodiscard]] std::string FixApp<Derived, ReadThreadName, WriteThreadName>::create_log_on(
    const std::string& sig_b64, const std::string& timestamp) {
  return static_cast<Derived*>(this)->create_log_on_message(sig_b64,
    timestamp);
}

template <typename Derived, FixedString ReadThreadName, FixedString WriteThreadName>
std::string FixApp<Derived, ReadThreadName, WriteThreadName>::create_heartbeat(
    FIX8::Message* message) {
  return static_cast<Derived*>(this)->create_heartbeat_message(message);
}

template <typename Derived, FixedString ReadThreadName, FixedString WriteThreadName>
void FixApp<Derived, ReadThreadName, WriteThreadName>::encode(std::string& data, FIX8::Message* msg) const {
  auto* ptr = data.data();
  msg->encode(&ptr);
}

template <typename Derived, FixedString ReadThreadName, FixedString WriteThreadName>
std::string FixApp<Derived, ReadThreadName, WriteThreadName>::timestamp() {
  using std::chrono::days;
  using std::chrono::duration_cast;
  using std::chrono::system_clock;
  using std::chrono::year_month_day;

  using std::chrono::hours;
  using std::chrono::milliseconds;
  using std::chrono::minutes;
  using std::chrono::seconds;

  const auto now = system_clock::now();
  //FIX8 only supports ms
  const auto militime = floor<milliseconds>(now);

  const auto dp_time = floor<days>(militime);
  const auto ymd = year_month_day{dp_time};
  const auto time = militime - dp_time;

  const auto hour = duration_cast<hours>(time);
  const auto minute = duration_cast<minutes>(time - hour);
  const auto second = duration_cast<seconds>(time - hour - minute);
  const auto ms = duration_cast<milliseconds>(time - hour - minute - second);

  char buf[64];
  std::snprintf(
      buf, sizeof(buf), "%04d%02d%02d-%02d:%02d:%02d.%03ld",
      static_cast<int>(ymd.year()), static_cast<unsigned>(ymd.month()),
      static_cast<unsigned>(ymd.day()), static_cast<int>(hour.count()),
      static_cast<int>(minute.count()), static_cast<int>(second.count()),
      ms.count());
  return std::string(buf);
}

template <typename Derived, FixedString ReadThreadName, FixedString WriteThreadName>
bool FixApp<Derived, ReadThreadName, WriteThreadName>::strip_to_header(std::string& buffer) {
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

template <typename Derived, FixedString ReadThreadName, FixedString WriteThreadName>
std::string FixApp<Derived, ReadThreadName, WriteThreadName>::get_signature_base64(
    const std::string& timestamp) const {
  EVP_PKEY* private_key = Util::load_ed25519(
      authorization_.pem_file_path,
      authorization_.private_password.c_str());

  // payload = "A<SOH>Sender<SOH>Target<SOH>1<SOH>20250709-00:49:41.041346"
  const std::string payload = std::string("A") + SOH + sender_id_ + SOH +
                              target_id_ + SOH + "1" + SOH + timestamp;

  return Util::sign_and_base64(private_key, payload);
}

template <typename Derived, FixedString ReadThreadName, FixedString WriteThreadName>
bool FixApp<Derived, ReadThreadName, WriteThreadName>::peek_full_message_len(const std::string& buffer,
                                                 size_t& msg_len) {
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

template <typename Derived, FixedString ReadThreadName, FixedString WriteThreadName>
bool FixApp<Derived, ReadThreadName, WriteThreadName>::extract_next_message(std::string& buffer,
                                                std::string& msg) {
  if (!strip_to_header(buffer))
    return false;

  size_t msg_len = 0;
  if (!peek_full_message_len(buffer, msg_len))
    return false;

  msg = buffer.substr(0, msg_len);
  buffer.erase(0, msg_len);
  return true;
}

template <typename Derived, FixedString ReadThreadName, FixedString WriteThreadName>
void FixApp<Derived, ReadThreadName, WriteThreadName>::process_message(const std::string& raw_msg) {
  auto* msg = static_cast<Derived*>(this)->decode(raw_msg);
  const auto type = msg->get_msgtype();

  if (UNLIKELY(type == "A")) {
    log_on_ = true;
  }

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

template class FixApp<FixMarketDataApp, "MDRead", "MDWrite">;
template class FixApp<FixOrderEntryApp, "OERead", "OEWrite">;
}