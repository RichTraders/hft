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

#ifndef SPOT_WS_OE_DECODER_H
#define SPOT_WS_OE_DECODER_H

#include <string_view>

#include <glaze/glaze.hpp>
#include "exchanges/binance/spot/binance_spot_oe_traits.h"
#include "schema/spot/response/account_position.h"
#include "ws_oe_decoder_base.hpp"

namespace core {

class SpotWsOeDecoder : public WsOeDecoderBase<SpotWsOeDecoder> {
  friend class WsOeDecoderBase<SpotWsOeDecoder>;

 public:
  using WireMessage = BinanceSpotOeTraits::WireMessage;
  using WsOeDecoderBase::WsOeDecoderBase;

 private:
  [[nodiscard]] WireMessage decode_impl(std::string_view payload) const {
    if (payload.empty()) {
      return WireMessage{};
    }
    logger_.debug("[WsOeCore]payload :{}", payload);

    if (payload.contains("executionReport")) {
      return this->decode_or_log<BinanceSpotOeTraits::ExecutionReportResponse,
          "[executionReport]">(payload);
    }
    if (payload.contains("outboundAccountPosition")) {
      return this->decode_or_log<schema::OutboundAccountPositionEnvelope,
          "[outboundAccountPosition]">(payload);
    }
    if (payload.contains("balanceUpdate")) {
      return this
          ->decode_or_log<schema::BalanceUpdateEnvelope, "[balanceUpdate]">(
              payload);
    }

    schema::WsHeader header{};  // NOLINT(misc-const-correctness)
    const auto error_code =
        glz::read<glz::opts{.error_on_unknown_keys = 0, .partial_read = 1}>(
            header,
            payload);
    if (error_code != glz::error_code::none) {
      logger_.error("Failed to decode payload");
      return WireMessage{};
    }
    logger_.debug("[WsOeCore]header id :{}", header.id);

    if (header.id.starts_with("login_")) {
      return this->decode_or_log<BinanceSpotOeTraits::SessionLogonResponse,
          "[session.logon]">(payload);
    }

    if (header.id.starts_with("subscribe")) {
      return this
          ->decode_or_log<BinanceSpotOeTraits::SessionUserSubscriptionResponse,
              "[userDataStream.subscribe]">(payload);
    }

    if (header.id.starts_with("unsubscribe")) {
      return this->decode_or_log<
          BinanceSpotOeTraits::SessionUserUnsubscriptionResponse,
          "[userDataStream.unsubscribe]">(payload);
    }

    if (header.id.starts_with("order")) {
      if (header.id.starts_with("orderreplace")) {
        return this
            ->decode_or_log<BinanceSpotOeTraits::CancelAndReorderResponse,
                "[cancelReplace]">(payload);
      }
      if (header.id.starts_with("ordercancelAll")) {
        return this->decode_or_log<BinanceSpotOeTraits::CancelAllOrdersResponse,
            "[cancelAll]">(payload);
      }
      if (header.id.starts_with("ordercancel")) {
        return this->decode_or_log<BinanceSpotOeTraits::CancelOrderResponse,
            "[orderCancel]">(payload);
      }
      return this->decode_or_log<BinanceSpotOeTraits::PlaceOrderResponse,
          "[orderPlace]">(payload);
    }

    return this
        ->decode_or_log<BinanceSpotOeTraits::ApiResponse, "[API response]">(
            payload);
  }
};

}  // namespace core

#endif  // SPOT_WS_OE_DECODER_H
