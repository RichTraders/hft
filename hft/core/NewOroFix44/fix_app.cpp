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

namespace core {

template <typename Derived, int Cpu>
FixApp<Derived, Cpu>::FixApp(const std::string& address, int port,
                             std::string sender_comp_id,
                             std::string target_comp_id, common::Logger* logger)
  : logger_(logger),
    tls_sock_(std::make_unique<SSLSocket>(address, port)),
    queue_(std::make_unique<common::SPSCQueue<std::string>>(kQueueSize)),
    sender_id_(std::move(sender_comp_id)),
    target_id_(std::move(target_comp_id)) {
  write_thread_.start(&FixApp::write_loop, this);
  read_thread_.start(&FixApp::read_loop, this);
}

template <typename Derived, int Cpu>
FixApp<Derived, Cpu>::~FixApp() {
  const auto msg = static_cast<Derived*>(this)->create_log_out_message();
  tls_sock_->write(msg.data(), static_cast<int>(msg.size()));

  thread_running_ = false;
}

template <typename Derived, int Cpu>
int FixApp<Derived, Cpu>::start() {
  const std::string cur_timestamp = timestamp();
  const std::string sig_b64 = get_signature_base64(cur_timestamp);

  const std::string fixmsg = create_log_on(sig_b64, cur_timestamp);

  send(fixmsg);
  std::cout << "log on sent\n";
  return 0;
}

template <typename Derived, int Cpu>
int FixApp<Derived, Cpu>::stop() {
  const auto msg = create_log_out();
  tls_sock_->write(msg.data(), static_cast<int>(msg.size()));
  thread_running_ = false;
  return 0;
}

template <typename Derived, int Cpu>
bool FixApp<Derived, Cpu>::send(const std::string& msg) const {
  return queue_->enqueue(msg);
}

template <typename Derived, int Cpu>
void FixApp<Derived, Cpu>::write_loop() {
  while (thread_running_) {
    std::string msg;

    while (queue_->dequeue(msg)) {
#ifdef DEBUG
        START_MEASURE(TLS_WRITE);
#endif
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
#ifdef DEBUG
        END_MEASURE(TLS_WRITE, logger_);
#endif
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kWriteThreadSleep));
  }
}

template <typename Derived, int Cpu>
void FixApp<Derived, Cpu>::read_loop() {
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

template <typename Derived, int Cpu>
void FixApp<Derived, Cpu>::register_callback(const MsgType& type,
                                             const std::function<void(
                                                 FIX8::Message*)>& callback) {
  if (!callbacks_.contains(type)) {
    callbacks_[type] = callback;
  } else {
    std::cout << "already registered type" << type << "\n";
  }
}

template <typename Derived, int Cpu>
[[nodiscard]] std::string FixApp<Derived, Cpu>::create_log_on(
    const std::string& sig_b64, const std::string& timestamp) {
  return static_cast<Derived*>(this)->create_log_on_message(sig_b64,
    timestamp);
}

template <typename Derived, int Cpu>
[[nodiscard]] std::string FixApp<Derived, Cpu>::create_log_out() {
  return static_cast<Derived*>(this)->create_log_out_message();
}

template <typename Derived, int Cpu>
std::string FixApp<Derived, Cpu>::create_heartbeat(
    FIX8::Message* message) {
  return static_cast<Derived*>(this)->create_heartbeat_message(message);
}

template <typename Derived, int Cpu>
void FixApp<Derived, Cpu>::encode(std::string& data, FIX8::Message* msg) const {
  auto* ptr = data.data();
  msg->encode(&ptr);
}

template <typename Derived, int Cpu>
std::string FixApp<Derived, Cpu>::timestamp() {
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

template <typename Derived, int Cpu>
void FixApp<Derived, Cpu>::process_message(const std::string& raw_msg) {
  auto* msg = static_cast<Derived*>(this)->decode(raw_msg);
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

template class FixApp<FixMarketDataApp>;
template class FixApp<FixOrderEntryApp, 3>;
}