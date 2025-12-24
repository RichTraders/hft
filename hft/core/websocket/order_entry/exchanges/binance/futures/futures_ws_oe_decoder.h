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

#include <cstddef>
#include <string_view>

#include <glaze/glaze.hpp>
#include "binance_futures_oe_traits.h"
#include "oe_id_constants.h"
#include "websocket/order_entry/ws_oe_decoder_base.hpp"

namespace core {

namespace oe_decode {
constexpr size_t kEventTypeOffset = sizeof(R"({"e":")") - 1;
constexpr size_t kIdOffset = sizeof(R"({"id":")") - 1;
constexpr size_t kMinPayloadLen = 8;
}  // namespace oe_decode

class FuturesWsOeDecoder : public WsOeDecoderBase<FuturesWsOeDecoder> {
  friend class WsOeDecoderBase<FuturesWsOeDecoder>;

 public:
  using WireMessage = BinanceFuturesOeTraits::WireMessage;
  using WsOeDecoderBase::WsOeDecoderBase;

 private:
  [[nodiscard]] WireMessage decode_impl(std::string_view payload) const {
    if (payload.size() < oe_decode::kMinPayloadLen) [[unlikely]] {
      return WireMessage{};
    }
    logger_.info("[WsOeCore]payload :{}", payload);

    const char event_char = payload[oe_decode::kEventTypeOffset];

    if (event_char == 'O') {
      return this
          ->decode_or_log<BinanceFuturesOeTraits::ExecutionReportResponse,
              "[executionReport]">(payload);
    }

    if (event_char == 'A') {
      return this->decode_or_log<BinanceFuturesOeTraits::BalanceUpdateEnvelope,
          "[accountUpdate]">(payload);
    }

    if (event_char == 'l') {
      return this->decode_or_log<BinanceFuturesOeTraits::ListenKeyExpiredEvent,
          "[listenKeyExpired]">(payload);
    }

    if (payload[2] == 'i') {
      return decode_id_response(payload);
    }

    return this
        ->decode_or_log<BinanceFuturesOeTraits::ApiResponse, "[API response]">(
            payload);
  }

  [[nodiscard]] WireMessage decode_id_response(std::string_view payload) const {
    const char id_char = payload[oe_decode::kIdOffset];

    switch (id_char) {
      case oe_id::kSubscribe:
        return this->decode_or_log<
            BinanceFuturesOeTraits::SessionUserSubscriptionResponse,
            "[userDataStream.subscribe]">(payload);

      case oe_id::kUnsubscribe:
        return this->decode_or_log<
            BinanceFuturesOeTraits::SessionUserUnsubscriptionResponse,
            "[userDataStream.unsubscribe]">(payload);

      case oe_id::kLogin:
        return this->decode_or_log<BinanceFuturesOeTraits::SessionLogonResponse,
            "[session.logon]">(payload);

      case oe_id::kOrderPlace:
        return this->decode_or_log<BinanceFuturesOeTraits::PlaceOrderResponse,
            "[orderPlace]">(payload);

      case oe_id::kOrderCancel:
        return this->decode_or_log<BinanceFuturesOeTraits::CancelOrderResponse,
            "[orderCancel]">(payload);

      case oe_id::kOrderReplace:
        return this
            ->decode_or_log<BinanceFuturesOeTraits::CancelAndReorderResponse,
                "[cancelReplace]">(payload);

      case oe_id::kOrderModify:
        return this
            ->decode_or_log<BinanceFuturesOeTraits::CancelAndReorderResponse,
                "[orderModify]">(payload);

      default:
        break;
    }

    return this
        ->decode_or_log<BinanceFuturesOeTraits::ApiResponse, "[API response]">(
            payload);
  }
};

}  // namespace core

#endif  // FUTURES_WS_OE_DECODER_H
