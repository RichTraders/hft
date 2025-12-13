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

#ifndef SBE_MD_DECODER_H
#define SBE_MD_DECODER_H

#include "exchange_traits.h"
#include "schema/spot/response/api_response.h"
#include "schema/spot/response/exchange_info_response.h"
#include "ws_md_decoder_base.hpp"

namespace core {

template <ExchangeTraits Exchange>
class SbeMdDecoder : public WsMdDecoderBase<SbeMdDecoder<Exchange>> {
  friend class WsMdDecoderBase<SbeMdDecoder<Exchange>>;

 public:
  using ExchangeTraits = Exchange;
  using Ops = Exchange::SbeOps;

  static_assert(Exchange::supports_sbe(),
      "This exchange does not support SBE encoding");

  using WireMessage = std::variant<std::monostate,
      typename Exchange::DepthResponse, typename Exchange::TradeEvent,
      typename Exchange::DepthSnapshot, typename Exchange::SbeDepthResponse,
      typename Exchange::SbeDepthSnapshot, typename Exchange::SbeTradeEvent,
      typename Exchange::SbeBestBidAsk, typename Exchange::ExchangeInfoResponse,
      typename Exchange::ApiResponse>;

  static constexpr std::string_view protocol_name() { return "SBE"; }
  static constexpr bool requires_api_key() { return true; }

  using WsMdDecoderBase<SbeMdDecoder<Exchange>>::WsMdDecoderBase;

 private:
  // TODO(JB): Move to binance decoder
  [[nodiscard]] WireMessage decode_impl(std::string_view payload) const {

    if (UNLIKELY(payload.empty())) {
      return WireMessage{};
    }

    if (payload == "__CONNECTED__") {
      return WireMessage{};
    }

    const char* pos = payload.data();
    typename Ops::MessageHeader header;
    std::memcpy(&header, pos, Ops::kHeaderSize);
    pos += Ops::kHeaderSize;

    const size_t remaining = payload.size() - Ops::kHeaderSize;

    static const Ops kOps;
    switch (header.template_id) {
      case Ops::kTradesStreamEventId:  // TradesStreamEvent
        return kOps.decode_trade_event(pos, remaining, this->logger_);

      case Ops::kBestBidAskStreamEventId:  // BestBidAskStreamEvent
        return kOps.decode_best_bid_ask(pos, remaining, this->logger_);

      case Ops::kDepthSnapshotStreamEventId:  // DepthSnapshotStreamEvent
        return kOps.decode_depth_snapshot(pos, remaining, this->logger_);

      case Ops::kDepthDiffStreamEventId:  // DepthDiffStreamEvent
        return kOps.decode_depth_diff(pos, remaining, this->logger_);

      default:
        constexpr int kPayloadMinLength = 200;
        this->logger_.warn(
            "Unknown SBE template ID: {} (schema_id={}, version={}) payload:{}",
            header.template_id,
            header.schema_id,
            header.version,
            payload.substr(0,
                std::min<size_t>(kPayloadMinLength, payload.size())));
        return {};
    }
  }
};

}  // namespace core
#endif  //SBE_MD_DECODER_H
