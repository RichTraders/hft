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

#ifndef CONNECTION_HANDLER_H
#define CONNECTION_HANDLER_H

#include <concepts>
#include <cstdint>
#include <string>

namespace core {

enum class TransportId : uint8_t { kApi = 0, kStream = 1 };

template <typename T>
concept ConnectionHandler = requires {
  requires requires(typename T::template Context<int>& ctx, TransportId tid) {
    { T::on_connected(ctx, tid) } -> std::same_as<void>;
  };
};

template <typename App>
struct ConnectionContext {
  App* app;
  TransportId transport_id;

  explicit ConnectionContext(App* app, TransportId tid)
      : app(app), transport_id(tid) {}

  [[nodiscard]] bool send(const std::string& msg) const {
    return app->send(msg);
  }

  [[nodiscard]] bool send_to_stream(const std::string& msg) const {
    if constexpr (requires { app->send_to_stream(msg); }) {
      return app->send_to_stream(msg);
    }
    return false;
  }
};

}  // namespace core

#endif  // CONNECTION_HANDLER_H
