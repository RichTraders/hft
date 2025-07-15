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

#include "ssl_socket.h"
#include "fix_wrapper.h"
#include "common/spsc_queue.h"
#include <fix8/f8includes.hpp>
#include <csignal>
#include <sys/epoll.h>

#define SPSCQueueSize 8

namespace core {
FixApp::FixApp(const std::string& address, int port,
               const std::string& sender_comp_id,
               const std::string& target_comp_id):
  fix_(std::make_unique<Fix>(sender_comp_id, target_comp_id)),
  tls_sock_(std::make_unique<SSLSocket>(address, port)),
  queue_(std::make_unique<common::SPSCQueue<std::string>>(SPSCQueueSize)),
  sender_id_(sender_comp_id),
  target_id_(target_comp_id) {

  write_thread_.start(&FixApp::write_loop, this);
  read_thread_.start(&FixApp::read_loop, this);
}

FixApp::~FixApp() {
  auto msg = fix_->create_log_out_message();
  tls_sock_->write(msg.data(), msg.size());

  thread_running = false;
}

void FixApp::register_callback(MsgType type,
                               std::function<void(FIX8::Message*)> cb) {
  if (!callbacks_.contains(type)) {
    callbacks_[type] = cb;
  } else {
    std::cout << "already registered type" << type << "\n";
  }
}

int FixApp::start() {
  const auto timestamp = fix_->timestamp();
  const std::string sig_b64 = fix_->get_sigature_base64(timestamp);
  const std::string fixmsg = fix_->create_log_on_message(sig_b64, timestamp);

  std::cout << "fixmesage : " << fixmsg << "\n";
  send(fixmsg);
  return 0;
}

int FixApp::stop() {
  auto msg = fix_->create_log_out_message();
  tls_sock_->write(msg.data(), msg.size());
  thread_running = false;
  return 0;
}

int FixApp::send(const std::string& msg) {
  queue_->enqueue(msg);

  return 0;
}


void FixApp::write_loop() {
  while (thread_running) {
    std::string msg;

    while (queue_->dequeue(msg)) {
      ssize_t result = tls_sock_->write(msg.data(),
                                        static_cast<int>(msg.size()));
      if (result < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          queue_->enqueue(msg);
          break;
        } else {
          perror("send failed");
          thread_running = false;
          break;
        }
      }
    }
    std::this_thread::yield();
  }
}

void FixApp::read_loop() {
  std::string received_buffer;
  while (thread_running) {
    char buf[2048];
    int kRead = tls_sock_->read(buf, sizeof(buf) - 1);
    if (kRead <= 0) {
      std::this_thread::yield();
      continue;
    }

    received_buffer.append(buf, kRead);

    size_t msg_len;
    while (has_full_fix_message(received_buffer, msg_len)) {
      auto msg = fix_->get_data(received_buffer);
      auto type = msg->get_msgtype();

      if (unlikely(!callbacks_.contains(type))) {
        delete msg;
        continue;
      }

      callbacks_[type](msg);
      delete msg;
      received_buffer.clear();
    }
  }
}

std::string FixApp::create_log_on_message(const std::string& sig_b64,
                                          const std::string& timestamp) {
  return fix_->create_log_on_message(sig_b64, timestamp);
}

std::string FixApp::create_log_out_message() {
  return fix_->create_log_out_message();
}

std::string FixApp::create_heartbeat_message() {
  return fix_->create_heartbeat_message();
}

std::string FixApp::create_subscription_message(const RequestId& request_id,
                                                const MarketDepthLevel& level,
                                                const SymbolId& symbol) {
  return fix_->create_market_data_subscription_message(request_id, level, symbol);
}

bool FixApp::has_full_fix_message(const std::string& buffer, size_t& msg_len) {
  size_t begin = buffer.find("8=FIX");
  if (begin == std::string::npos) {
    return false;
  }

  size_t body_start = buffer.find("9=", begin);
  if (body_start == std::string::npos)
    return false;

  size_t body_end = buffer.find('\x01', body_start);
  if (body_end == std::string::npos)
    return false;

  int body_len = std::stoi(
      buffer.substr(body_start + 2, body_end - (body_start + 2)));
  size_t header_len = body_end + 1 - begin;
  msg_len = header_len + body_len + 7;

  if (buffer.size() < begin + msg_len)
    return false;

  return true;
}
}