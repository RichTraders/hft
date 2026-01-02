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

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <curl/curl.h>

#include "http_client.h"

namespace core::http {

namespace {
constexpr int64_t kDefaultTimeoutMs = 30000;
constexpr int64_t kDefaultConnectTimeoutMs = 10000;
constexpr int64_t kDisableSignal = 1L;
constexpr int64_t kEnablePost = 1L;

size_t write_callback(void* contents, size_t size, size_t nmemb,
    std::string* output) {
  const size_t new_length = size * nmemb;
  output->append(static_cast<char*>(contents), new_length);
  return new_length;
}
}  // namespace

struct HttpClient::Impl {
  CURL* curl{nullptr};
  int64_t timeout_ms{kDefaultTimeoutMs};
  int64_t connect_timeout_ms{kDefaultConnectTimeoutMs};

  Impl() { curl = curl_easy_init(); }

  ~Impl() {
    if (curl) {
      curl_easy_cleanup(curl);
    }
  }

  Impl(Impl&& other) noexcept
      : curl(other.curl),
        timeout_ms(other.timeout_ms),
        connect_timeout_ms(other.connect_timeout_ms) {
    other.curl = nullptr;
  }

  Impl& operator=(Impl&& other) noexcept {
    if (this != &other) {
      if (curl) {
        curl_easy_cleanup(curl);
      }
      curl = other.curl;
      timeout_ms = other.timeout_ms;
      connect_timeout_ms = other.connect_timeout_ms;
      other.curl = nullptr;
    }
    return *this;
  }
};

HttpClient::HttpClient() : impl_(std::make_unique<Impl>()) {}

HttpClient::~HttpClient() = default;

HttpClient::HttpClient(HttpClient&&) noexcept = default;

HttpClient& HttpClient::operator=(HttpClient&&) noexcept = default;

void HttpClient::set_timeout(int64_t timeout_ms) const {
  impl_->timeout_ms = timeout_ms;
}

void HttpClient::set_connect_timeout(int64_t timeout_ms) const {
  impl_->connect_timeout_ms = timeout_ms;
}

HttpResponse HttpClient::perform_request(const std::string& url,
    const std::string& method, const std::string& body,
    const std::vector<std::string>& headers) const {
  HttpResponse response;

  if (!impl_->curl) {
    response.error = "Failed to initialize CURL";
    return response;
  }

  CURL* curl = impl_->curl;
  curl_easy_reset(curl);

  std::string response_body;

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, impl_->timeout_ms);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, impl_->connect_timeout_ms);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, kDisableSignal);

  if (method == "POST") {
    curl_easy_setopt(curl, CURLOPT_POST, kEnablePost);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl,
        CURLOPT_POSTFIELDSIZE,
        static_cast<long>(body.size()));
  } else if (method == "PUT") {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    if (!body.empty()) {
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
      curl_easy_setopt(curl,
          CURLOPT_POSTFIELDSIZE,
          static_cast<long>(body.size()));
    }
  } else if (method == "DELETE") {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
  }

  struct curl_slist* header_list = nullptr;
  for (const auto& header : headers) {
    header_list = curl_slist_append(header_list, header.c_str());
  }
  if (header_list) {
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
  }

  const CURLcode res = curl_easy_perform(curl);

  if (res != CURLE_OK) {
    response.error = curl_easy_strerror(res);
  } else {
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status_code);
    response.body = std::move(response_body);
  }

  if (header_list) {
    curl_slist_free_all(header_list);
  }

  return response;
}

HttpResponse HttpClient::get(const std::string& url,
    const std::vector<std::string>& headers) const {
  return perform_request(url, "GET", "", headers);
}

HttpResponse HttpClient::post(const std::string& url, const std::string& body,
    const std::vector<std::string>& headers) const {
  return perform_request(url, "POST", body, headers);
}

HttpResponse HttpClient::put(const std::string& url, const std::string& body,
    const std::vector<std::string>& headers) const {
  return perform_request(url, "PUT", body, headers);
}

HttpResponse HttpClient::del(const std::string& url,
    const std::vector<std::string>& headers) const {
  return perform_request(url, "DELETE", "", headers);
}

}  // namespace core::http
