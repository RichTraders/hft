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
#include <pch.h>
#include <openssl/ssl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/tcp.h>

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

  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  int flag = 1;
  setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int));
  int const ret = connect(sockfd, reinterpret_cast<sockaddr*>(&serv_addr),
                          sizeof(serv_addr));
  if (ret < 0) {
    throw std::runtime_error("socket connection failed");
  }

  SSL_library_init();
  ctx = SSL_CTX_new(TLS_client_method());
  ssl = SSL_new(ctx);
  SSL_set_fd(ssl, sockfd);

  if (SSL_connect(ssl) <= 0) {
    ERR_print_errors_fp(stderr);
    throw std::runtime_error("SSL connection failed");
  }
  std::cout << "ssl connected\n";
}

int SSLSocket::read(char* buf, int len) const {
  return SSL_read(ssl, buf, len);
}

int SSLSocket::write(const char* buf, int len) const {
  return SSL_write(ssl, buf, len);
}

SSLSocket::~SSLSocket() {
  SSL_shutdown(ssl);
  SSL_free(ssl);
  close(sockfd);
  SSL_CTX_free(ctx);
}
}