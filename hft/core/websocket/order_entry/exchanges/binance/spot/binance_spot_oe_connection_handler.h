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

#ifndef BINANCE_SPOT_OE_CONNECTION_HANDLER_H
#define BINANCE_SPOT_OE_CONNECTION_HANDLER_H

#include "websocket/connection_handler.h"

struct BinanceSpotOeConnectionHandler {
  template <typename App>
  using Context = core::ConnectionContext<App>;

  template <typename App>
  [[gnu::always_inline]] static void on_connected(Context<App>& ctx,
      core::TransportId tid) {
    if (tid == core::TransportId::kApi) {
      ctx.app->initiate_session_logon();
    }
  }

  template <typename App, typename Response>
  [[gnu::always_inline]] static void on_session_logon(Context<App>& ctx,
      const Response& response) {
    if (response.status == kHttpOK) {
      ctx.app->send(ctx.app->create_user_data_stream_subscribe());
    }
  }

  template <typename App, typename Response>

  [[gnu::always_inline]] static void on_user_subscription(Context<App>& ctx,
      const Response& response) {
    if (response.status == kHttpOK) {
      ctx.app->set_session_ready();
    }
  }

 private:
  static constexpr int kHttpOK = 200;
};

#endif  // BINANCE_SPOT_OE_CONNECTION_HANDLER_H
