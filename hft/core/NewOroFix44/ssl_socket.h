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

#pragma once

#include <openssl/err.h>

namespace core {
class SSLSocket {
public:
  SSLSocket(const std::string& host, int port);
  ~SSLSocket();

  int read(char* buf, int len) const;

  int write(const char* buf, int len) const;

private:
  int sockfd;
  SSL_CTX* ctx;
  SSL* ssl;
};
}