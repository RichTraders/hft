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

#ifndef FUTURES_WS_OE_DECODER_H
#define FUTURES_WS_OE_DECODER_H

#include <glaze/glaze.hpp>
#include "binance_futures_oe_traits.h"
#include "websocket/order_entry/ws_oe_decoder_base.hpp"

namespace core {

class FuturesWsOeDecoder : public WsOeDecoderBase<FuturesWsOeDecoder> {
  friend class WsOeDecoderBase<FuturesWsOeDecoder>;

 public:
  using WireMessage = BinanceFuturesOeTraits::WireMessage;
  using WsOeDecoderBase::WsOeDecoderBase;

 private:
  [[nodiscard]] WireMessage decode_impl(std::string_view payload) const {
    if (payload.empty()) {
      return WireMessage{};
    }
    logger_.info("[WsOeCore]payload :{}", payload);

    if (payload.contains("ORDER_TRADE_UPDATE")) {
      return this
          ->decode_or_log<BinanceFuturesOeTraits::ExecutionReportResponse,
              "[executionReport]">(payload);
    }

    schema::WsHeader header{};
    const auto error_code =
        glz::read<glz::opts{.error_on_unknown_keys = 0, .partial_read = 1}>(
            header,
            payload);
    if (error_code != glz::error_code::none) {
      logger_.error("Failed to decode payload");
      return WireMessage{};
    }
    logger_.debug("[WsOeCore]header id :{}", header.id);

    if (header.id.starts_with("subscribe")) {
      return this->decode_or_log<
          BinanceFuturesOeTraits::SessionUserSubscriptionResponse,
          "[userDataStream.subscribe]">(payload);
    }

    if (header.id.starts_with("unsubscribe")) {
      return this->decode_or_log<
          BinanceFuturesOeTraits::SessionUserUnsubscriptionResponse,
          "[userDataStream.unsubscribe]">(payload);
    }

    if (header.id.starts_with("login_")) {
      return this->decode_or_log<BinanceFuturesOeTraits::SessionLogonResponse,
          "[session.logon]">(payload);
    }

    if (header.id.starts_with("order")) {
      if (header.id.starts_with("orderreplace")) {
        return this
            ->decode_or_log<BinanceFuturesOeTraits::CancelAndReorderResponse,
                "[cancelReplace]">(payload);
      }
      if (header.id.starts_with("ordercancel")) {
        return this->decode_or_log<BinanceFuturesOeTraits::CancelOrderResponse,
            "[orderCancel]">(payload);
      }
      return this->decode_or_log<BinanceFuturesOeTraits::PlaceOrderResponse,
          "[orderPlace]">(payload);
    }

    return this
        ->decode_or_log<BinanceFuturesOeTraits::ApiResponse, "[API response]">(
            payload);
  }
};

}  // namespace core

#endif  // FUTURES_WS_OE_DECODER_H
