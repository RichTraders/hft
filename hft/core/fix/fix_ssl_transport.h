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

#ifndef FIX_SSL_TRANSPORT_H
#define FIX_SSL_TRANSPORT_H

#include "core/ssl_socket.h"

namespace core {

class FixSslTransport {
 public:
  FixSslTransport(const std::string& host, int port)
      : socket_(std::make_unique<SSLSocket>(host, port)) {}

  int read(char* buffer, int length) const {
    return socket_->read(buffer, length);
  }

  int write(const char* buffer, int length) const {
    return socket_->write(buffer, length);
  }

  void interrupt() const { socket_->interrupt(); }

 private:
  std::unique_ptr<SSLSocket> socket_;
};

}  // namespace core

#endif
