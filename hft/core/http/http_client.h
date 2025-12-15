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

#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include "pch.h"

namespace core::http {

namespace status {
constexpr int64_t kOk = 200;
constexpr int64_t kMultipleChoices = 300;
}  // namespace status

struct HttpResponse {
  int64_t status_code{0};
  std::string body;
  std::string error;

  [[nodiscard]] bool ok() const {
    return status_code >= status::kOk && status_code < status::kMultipleChoices;
  }
};

class HttpClient {
 public:
  HttpClient();
  ~HttpClient();

  HttpClient(const HttpClient&) = delete;
  HttpClient& operator=(const HttpClient&) = delete;
  HttpClient(HttpClient&&) noexcept;
  HttpClient& operator=(HttpClient&&) noexcept;

  [[nodiscard]] HttpResponse get(const std::string& url,
      const std::vector<std::string>& headers = {}) const;

  [[nodiscard]] HttpResponse post(const std::string& url,
      const std::string& body = "",
      const std::vector<std::string>& headers = {}) const;

  [[nodiscard]] HttpResponse put(const std::string& url,
      const std::string& body = "",
      const std::vector<std::string>& headers = {}) const;

  [[nodiscard]] HttpResponse del(const std::string& url,
      const std::vector<std::string>& headers = {}) const;

  void set_timeout(int64_t timeout_ms) const;
  void set_connect_timeout(int64_t timeout_ms) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  [[nodiscard]] HttpResponse perform_request(const std::string& url,
      const std::string& method, const std::string& body,
      const std::vector<std::string>& headers) const;
};

}  // namespace core::http

#endif  // HTTP_CLIENT_H
