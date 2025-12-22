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

#ifndef BINANCE_FUTURES_OE_CONNECTION_HANDLER_H
#define BINANCE_FUTURES_OE_CONNECTION_HANDLER_H

#include "websocket/connection_handler.h"

struct BinanceFuturesOeConnectionHandler {
  template <typename App>
  using Context = core::ConnectionContext<App>;

  template <typename App>
  [[gnu::always_inline]] static void on_connected(Context<App>& ctx,
      core::TransportId tid) {
    if (tid == core::TransportId::kApi) {
      ctx.app->initiate_session_logon();
    } else if (tid == core::TransportId::kStream) {
      ctx.app->start_listen_key_keepalive();
      ctx.app->set_session_ready();
    }
  }

  template <typename App, typename Response>
  [[gnu::always_inline]] static void on_session_logon(Context<App>& /*ctx*/,
      const Response& /*response*/) {}

  template <typename App, typename Response>
  [[gnu::always_inline]] static void on_user_subscription(Context<App>& ctx,
      const Response& response) {
    if (response.status == kHttpOK && response.result.has_value()) {
      const auto& listen_key = response.result.value().listen_key;
      if (!listen_key.empty()) {
        ctx.app->handle_listen_key_response(listen_key);
      }
    }
  }

 private:
  static constexpr int kHttpOK = 200;
};

#endif  // BINANCE_FUTURES_OE_CONNECTION_HANDLER_H
