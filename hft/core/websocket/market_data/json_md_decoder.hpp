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

#ifndef JSON_MD_DECODER_H
#define JSON_MD_DECODER_H

#include "exchange_traits.h"
#include "ws_md_decoder_base.hpp"

namespace core {

template <ExchangeTraits Exchange>
class JsonMdDecoder : public WsMdDecoderBase<JsonMdDecoder<Exchange>> {
  friend class WsMdDecoderBase<JsonMdDecoder<Exchange>>;

 public:
  using ExchangeTraits = Exchange;

  // Use WireMessage from traits to avoid duplicate std::monostate types
  using WireMessage = typename Exchange::WireMessage;

  static constexpr std::string_view protocol_name() { return "JSON"; }
  static constexpr bool requires_api_key() { return false; }

  using WsMdDecoderBase<JsonMdDecoder<Exchange>>::WsMdDecoderBase;

 private:
  [[nodiscard]] WireMessage decode_impl(std::string_view payload) const {
    if (payload.empty()) {
      return WireMessage{};
    }
    if (payload == "__CONNECTED__") {
      return WireMessage{};
    }

    if (Exchange::is_depth_message(payload)) {
      return this->template decode_or_log<typename Exchange::DepthResponse,
          "[DepthStream]">(payload);
    }

    if (Exchange::is_trade_message(payload)) {
      return this->template decode_or_log<typename Exchange::TradeEvent,
          "[TradeStream]">(payload);
    }

    if constexpr (requires { Exchange::is_book_ticker_message(payload); }) {
      if (Exchange::is_book_ticker_message(payload)) {
        return this->template decode_or_log<typename Exchange::BookTickerEvent,
            "[BookTicker]">(payload);
      }
    }

    if constexpr (requires { typename Exchange::DepthSnapshot; }) {
      if (Exchange::is_snapshot_message(payload)) {
        return this->template decode_or_log<typename Exchange::DepthSnapshot,
            "[DepthSnapshot]">(payload);
      }
    }

    if constexpr (!std::is_same_v<typename Exchange::ExchangeInfoResponse,
                      std::monostate>) {
      if (payload.contains("exchangeInfo")) {
        typename Exchange::ExchangeInfoResponse exchange;
        auto error_code =
            glz::read<glz::opts{.error_on_unknown_keys = 0}>(exchange, payload);

        if (error_code != glz::error_code::none) {
          const std::string_view view{payload.data(), payload.size()};
          auto msg = glz::format_error(error_code, view);
          this->logger_.error(
              std::format("Failed to [ExchangeInfo] payload:{}. msg:{}",
                  payload,
                  msg));
          return WireMessage{};
        }
        return WireMessage{
            std::in_place_type<typename Exchange::ExchangeInfoResponse>,
            exchange};
      }
    }

    if (const auto api_response =
            glz::read_json<typename Exchange::ApiResponse>(payload)) {
      return WireMessage{std::in_place_type<typename Exchange::ApiResponse>,
          std::move(*api_response)};
    }

    constexpr int kDefaultMinMessageLen = 100;
    this->logger_.warn(std::format("Unhandled websocket payload: {}",
        payload.substr(0,
            std::min<size_t>(payload.size(), kDefaultMinMessageLen))));
    return WireMessage{};
  }
};

}  // namespace core
#endif  //JSON_MD_DECODER_H