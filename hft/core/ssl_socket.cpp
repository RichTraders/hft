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
#include "ssl_socket.h"
#include <netdb.h>
#include <netinet/tcp.h>
#include <openssl/ssl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace core {
SSLSocket::SSLSocket(const std::string& host, int port) {
  hostent* server = gethostbyname(host.c_str());
  if (server == nullptr) {
    throw std::runtime_error("server dns failed");
  }
  sockaddr_in serv_addr{};
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(port);
  memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);

  sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
  int flag = 1;
  setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int));
  int const ret = connect(sockfd_,
      reinterpret_cast<sockaddr*>(&serv_addr),
      sizeof(serv_addr));
  if (ret < 0) {
    throw std::runtime_error("socket connection failed");
  }

  ctx_ = SSL_CTX_new(TLS_client_method());
  ssl_ = SSL_new(ctx_);
  SSL_set_fd(ssl_, sockfd_);

  if (SSL_connect(ssl_) <= 0) {
    ERR_print_errors_fp(stderr);
    throw std::runtime_error("SSL connection failed");
  }
  std::cout << "ssl connected\n";
}

int SSLSocket::read(char* buf, int len) const {
  const int read = SSL_read(ssl_, buf, len);
  if (read > 0)
    return read;

  const int err = SSL_get_error(ssl_, read);
  switch (err) {
    case SSL_ERROR_ZERO_RETURN:
      return 0;
    case SSL_ERROR_WANT_READ:
    case SSL_ERROR_WANT_WRITE:
      errno = EAGAIN;
      return -1;
    case SSL_ERROR_SYSCALL:
      // errno를 그대로 사용 (EAGAIN일 수 있음)
      return -2;
    default:
      return -1;
  }
}

int SSLSocket::write(const char* buf, int len) const {
  return SSL_write(ssl_, buf, len);
}

SSLSocket::~SSLSocket() {
  SSL_shutdown(ssl_);
  SSL_free(ssl_);
  close(sockfd_);
  SSL_CTX_free(ctx_);
}

void SSLSocket::interrupt() {
  SSL_shutdown(ssl_);
  ::shutdown(sockfd_, SHUT_RDWR);
}
}  // namespace core