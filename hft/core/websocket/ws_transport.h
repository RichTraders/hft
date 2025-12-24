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
#ifndef WS_TRANSPORT_H
#define WS_TRANSPORT_H

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include <libwebsockets.h>

#include "common/spsc_queue.h"
#include "common/thread.hpp"
#include "global.h"

namespace core {
template <FixedString ThreadName>
class WebSocketTransport {
 public:
  using MessageCallback = std::function<void(std::string_view)>;

  WebSocketTransport();
  WebSocketTransport(std::string host, int port, std::string path = "/",
      bool use_ssl = true, bool notify_connected = false,
      std::string_view api_key = "");
  ~WebSocketTransport();

  void initialize(std::string host, int port, std::string path = "/",
      bool use_ssl = true, bool notify_connected = false,
      std::string_view api_key = "");

  WebSocketTransport(const WebSocketTransport&) = delete;
  WebSocketTransport& operator=(const WebSocketTransport&) = delete;

  void register_message_callback(MessageCallback callback);
  int write(const std::string& buffer) const;
  void interrupt();

  static int callback(struct lws* wsi, enum lws_callback_reasons reason,
      void* user, void* data, size_t len);

 private:
  static constexpr size_t kQueueSize = 32768;
  static constexpr size_t kWriteThreadSleep = 100;
  int handle_callback(struct lws* wsi, enum lws_callback_reasons reason,
      void* data, size_t len);

  void service_loop();

  std::string host_;
  std::string path_;
  int port_;
  bool use_ssl_;
  bool notify_connected_;
  std::string api_key_;

  lws_context* context_{nullptr};
  lws* wsi_{nullptr};

  common::Thread<ThreadName> service_thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> connected_{false};
  std::atomic<bool> interrupted_{false};
  std::atomic<bool> ready_{false};

  MessageCallback message_callback_;
  mutable std::unique_ptr<common::SPSCQueue<std::string, kQueueSize>> queue_;

  std::string fragment_buffer_;
};

}  // namespace core

#include "ws_transport.tpp"

#endif
