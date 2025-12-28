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

#ifndef WS_TRANSPORT_TPP
#define WS_TRANSPORT_TPP

#include "core/common.h"

namespace core {
namespace {

constexpr int kServiceIntervalMs = 50;
constexpr std::string_view kConnectedSignalString = "__CONNECTED__";

template <FixedString ThreadName>
struct Protocols {
  explicit Protocols()
      : entries{lws_protocols{"fix-websocket",
                    WebSocketTransport<ThreadName>::callback,
                    0,
                    64 * 1024,
                    0,
                    nullptr,
                    0},
            lws_protocols{nullptr, nullptr, 0, 0, 0, nullptr, 0}} {}

  std::array<lws_protocols, 2> entries;
};

}  // namespace

template <FixedString ThreadName>
WebSocketTransport<ThreadName>::WebSocketTransport() {
  running_.store(true, std::memory_order_release);
  service_thread_.start(&WebSocketTransport::service_loop, this);
  std::cout << "WebSocketTransport thread started (uninitialized)\n";
}

template <FixedString ThreadName>
WebSocketTransport<ThreadName>::WebSocketTransport(std::string host, int port,
    std::string path, bool use_ssl, bool notify_connected,
    std::string_view api_key)
    : host_(std::move(host)),
      path_(std::move(path)),
      port_(port),
      use_ssl_(use_ssl),
      notify_connected_(notify_connected),
      api_key_(std::string(api_key.data())),
      queue_(std::make_unique<common::SPSCQueue<std::string, kQueueSize>>()) {

  static Protocols<ThreadName> protocols;
  lws_context_creation_info info{};
  info.port = CONTEXT_PORT_NO_LISTEN;
  info.protocols = protocols.entries.data();
  if (use_ssl_) {
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
#ifdef __APPLE__
    info.client_ssl_ca_filepath = "/opt/homebrew/etc/openssl@3/cert.pem";
#else
    info.client_ssl_ca_filepath = "/etc/ssl/certs/ca-certificates.crt";
#endif
  }

  info.user = this;

  context_ = lws_create_context(&info);
  if (!context_) {
    throw std::runtime_error("WebSocketTransport: context creation failed");
  }

  lws_client_connect_info ccinfo{};
  ccinfo.context = context_;
  ccinfo.address = host_.c_str();
  ccinfo.port = port_;
  ccinfo.path = path_.c_str();
  ccinfo.host = host_.c_str();
  ccinfo.origin = host_.c_str();
  ccinfo.protocol = protocols.entries[0].name;
  ccinfo.userdata = this;
  ccinfo.ssl_connection = use_ssl_ ? LCCSCF_USE_SSL : 0;

  std::cout << "[WS] Connecting to " << host_ << ":" << port_ << path_
            << " (SSL: " << (use_ssl_ ? "yes" : "no") << ")\n";

  wsi_ = lws_client_connect_via_info(&ccinfo);
  if (!wsi_) {
    lws_context_destroy(context_);
    context_ = nullptr;
    std::cerr << "[WS] Connection failed: " << host_ << ":" << port_ << path_
              << "\n";
    throw std::runtime_error("WebSocketTransport: connection failed");
  }

  running_.store(true, std::memory_order_release);
  ready_.store(true,std::memory_order_release);
  service_thread_.start(&WebSocketTransport::service_loop, this);
  std::cout << "WebSocketTransport Created\n";
}

template <FixedString ThreadName>
void WebSocketTransport<ThreadName>::initialize(std::string host, int port,
    std::string path, bool use_ssl, bool notify_connected,
    std::string_view api_key) {
  host_ = std::move(host);
  path_ = std::move(path);
  port_ = port;
  use_ssl_ = use_ssl;
  notify_connected_ = notify_connected;
  api_key_ = std::string(api_key.data());
  queue_ = std::make_unique<common::SPSCQueue<std::string, kQueueSize>>();

  static Protocols<ThreadName> protocols;
  lws_context_creation_info info{};
  info.port = CONTEXT_PORT_NO_LISTEN;
  info.protocols = protocols.entries.data();
  if (use_ssl_) {
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
#ifdef __APPLE__
    info.client_ssl_ca_filepath = "/opt/homebrew/etc/openssl@3/cert.pem";
#else
    info.client_ssl_ca_filepath = "/etc/ssl/certs/ca-certificates.crt";
#endif
  }

  info.user = this;

  context_ = lws_create_context(&info);
  if (!context_) {
    throw std::runtime_error("WebSocketTransport: context creation failed");
  }

  lws_client_connect_info ccinfo{};
  ccinfo.context = context_;
  ccinfo.address = host_.c_str();
  ccinfo.port = port_;
  ccinfo.path = path_.c_str();
  ccinfo.host = host_.c_str();
  ccinfo.origin = host_.c_str();
  ccinfo.protocol = protocols.entries[0].name;
  ccinfo.userdata = this;
  ccinfo.ssl_connection = use_ssl_ ? LCCSCF_USE_SSL : 0;

  std::cout << "[WS] Connecting to " << host_ << ":" << port_ << path_
            << " (SSL: " << (use_ssl_ ? "yes" : "no") << ")\n";

  wsi_ = lws_client_connect_via_info(&ccinfo);
  if (!wsi_) {
    lws_context_destroy(context_);
    context_ = nullptr;
    std::cerr << "[WS] Connection failed: " << host_ << ":" << port_ << path_
              << "\n";
    throw std::runtime_error("WebSocketTransport: connection failed");
  }
  ready_.store(true,std::memory_order_release);

  std::cout << "WebSocketTransport initialized\n";
}

template <FixedString ThreadName>
WebSocketTransport<ThreadName>::~WebSocketTransport() {
  interrupt();

  running_.store(false, std::memory_order_release);
  if (context_) {
    lws_cancel_service(context_);
  }

  service_thread_.join();

  if (context_) {
    lws_context_destroy(context_);
    context_ = nullptr;
  }
}

template <FixedString ThreadName>
void WebSocketTransport<ThreadName>::register_message_callback(
    MessageCallback callback) {
  message_callback_ = std::move(callback);
}

template <FixedString ThreadName>
int WebSocketTransport<ThreadName>::write(const std::string& buffer) const {
  if (!connected_.load(std::memory_order_acquire) ||
      interrupted_.load(std::memory_order_acquire)) {
    std::cout << "[WebSocketTransport::write] write failed. connected:"
              << connected_.load() << ", interrupted: " << interrupted_.load()
              << std::endl;
    return -1;
  }

  /*
  std::cout << "[WebSocketTransport][" << ThreadName << "]: write:" << buffer
            << "\n";
  */

  std::string payload(LWS_PRE + buffer.size(), '\0');
  std::memcpy(payload.data() + LWS_PRE, buffer.data(), buffer.size());
  queue_->enqueue(std::move(payload));

  if (LIKELY(context_)) {
    lws_cancel_service(context_);
  }

  return static_cast<int>(buffer.size());
}

template <FixedString ThreadName>
void WebSocketTransport<ThreadName>::interrupt() {
  // TODO(jb): implement I'm not sure it can be interrupted
  interrupted_.store(true, std::memory_order_release);

  if (context_) {
    lws_cancel_service(context_);
  }
}

template <FixedString ThreadName>
int WebSocketTransport<ThreadName>::callback(struct lws* wsi,
    enum lws_callback_reasons reason, void* /*user*/, void* data, size_t len) {
  auto* transport =
      static_cast<WebSocketTransport*>(lws_context_user(lws_get_context(wsi)));
  if (!transport) {
    return 0;
  }
  return transport->handle_callback(wsi, reason, data, len);
}

template <FixedString ThreadName>
int WebSocketTransport<ThreadName>::handle_callback(struct lws* wsi,
    enum lws_callback_reasons reason, void* data, size_t len) {
  switch (reason) {
    case LWS_CALLBACK_CLIENT_ESTABLISHED: {
      connected_.store(true, std::memory_order_release);
      wsi_ = wsi;
      if (notify_connected_)
        message_callback_(kConnectedSignalString);
      break;
    }
    case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER: {
      unsigned char** p_data = static_cast<unsigned char**>(data);
      unsigned char* end = (*p_data) + len;

      if (api_key_.empty())
        break;
      const unsigned char* token =
          reinterpret_cast<const unsigned char*>(api_key_.data());

      if (lws_add_http_header_by_name(wsi,
              reinterpret_cast<const unsigned char*>("X-MBX-APIKEY:"),
              token,
              api_key_.size(),
              p_data,
              end)) {
        return -1;
      }
      break;
    }

    case LWS_CALLBACK_CLIENT_RECEIVE: {
      if (data && len > 0) {
        fragment_buffer_.append(static_cast<const char*>(data), len);

        const auto is_final = lws_is_final_fragment(wsi);
        const auto no_remaining = !lws_remaining_packet_payload(wsi);

        if (is_final && no_remaining) {
          if (LIKELY(message_callback_)) {
            message_callback_(fragment_buffer_);
          }
          fragment_buffer_.clear();
        }
      }
      break;
    }

    case LWS_CALLBACK_CLIENT_WRITEABLE: {
      std::string item;
      if (queue_->dequeue(item)) {
        const int result = lws_write(wsi,
            reinterpret_cast<unsigned char*>(item.data() + LWS_PRE),
            item.size() - LWS_PRE,
            LWS_WRITE_TEXT);
        if (result < 0) {
          connected_.store(false, std::memory_order_release);
          interrupted_.store(true, std::memory_order_release);
        }
        if (!queue_->empty()) {
          lws_callback_on_writable(wsi);
        }
      }
      break;
    }
    case LWS_CALLBACK_EVENT_WAIT_CANCELLED: {
      if (LIKELY(wsi_) && !queue_->empty()) {
        lws_callback_on_writable(wsi_);
      }
      break;
    }

    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
      std::string msg;

      if (data && len) {
        msg.assign(static_cast<const char*>(data), len);
      } else {
        msg = "unknown error";
      }

      std::cerr << "[WS][" << ThreadName << "] CLIENT_CONNECTION_ERROR: " << msg
                << "\n";
      [[fallthrough]];
    }
    case LWS_CALLBACK_CLIENT_CLOSED:
    case LWS_CALLBACK_CLOSED: {
      connected_.store(false, std::memory_order_release);
      interrupted_.store(true, std::memory_order_release);

      std::string msg;

      if (data && len) {
        msg.assign(static_cast<const char*>(data), len);
      } else {
        msg = "unknown error";
      }

      std::cerr << "[WS][" << ThreadName
                << "] CLIENT_CONNECTION_CLOSED: " << msg << "\n";
      break;
    }
    default:
      break;
  }

  return 0;
}

template <FixedString ThreadName>
void WebSocketTransport<ThreadName>::service_loop() {
  while (running_.load(std::memory_order_acquire)) {
    if (ready_.load(std::memory_order_acquire) && context_) {
      lws_service(context_, kServiceIntervalMs);
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(kServiceIntervalMs));
    }
  }
}

}  // namespace core

#endif