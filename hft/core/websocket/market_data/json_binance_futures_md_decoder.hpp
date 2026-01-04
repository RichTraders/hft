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

#ifndef JSON_BINANCE_FUTURES_MD_DECODER_H
#define JSON_BINANCE_FUTURES_MD_DECODER_H

#include <common/logger.h>
#include <glaze/glaze.hpp>

#include "global.h"
#include "schema/futures/response/api_response.h"
#include "schema/futures/response/book_ticker.h"
#include "schema/futures/response/depth_stream.h"
#include "schema/futures/response/exchange_info_response.h"
#include "schema/futures/response/snapshot.h"
#include "schema/futures/response/trade.h"

namespace core {

class JsonBinanceFuturesMdDecoder {
 public:
  using DepthResponse = schema::futures::DepthResponse;
  using TradeEvent = schema::futures::TradeEvent;
  using BookTickerEvent = schema::futures::BookTickerEvent;
  using DepthSnapshot = schema::futures::DepthSnapshot;
  using ApiResponse = schema::futures::ApiResponse;
  using ExchangeInfoResponse = schema::futures::ExchangeInfoHttpResponse;

  using WireMessage = std::variant<std::monostate, DepthResponse, TradeEvent,
      BookTickerEvent, DepthSnapshot, ApiResponse, ExchangeInfoResponse>;

  static constexpr std::string_view protocol_name() { return "JSON"; }
  static constexpr bool requires_api_key() { return false; }

  explicit JsonBinanceFuturesMdDecoder(const common::Logger::Producer& logger)
      : logger_(logger) {}

  [[nodiscard]] WireMessage decode(std::string_view payload) const {
    constexpr auto kMinimumPayloadLen = 10;
    if (payload.size() < kMinimumPayloadLen) [[unlikely]] {
      return WireMessage{};
    }

    // Fast dispatch based on payload prefix patterns
    // snapshot: {"id":"snapshot_XRPUSDC...
    // depth:    {"stream":"btcusdt@depth"...
    // trade:    {"stream":"btcusdt@aggTrade"...
    // exchange: {"timezone":"UTC","serverTime...
    // bookTicker: {"stream":"btcusdt@bookTicker"...

    const char first_chr = payload[2];  // First char after {"

    if (first_chr == 's') [[likely]] {
      // {"stream":...  - depth, trade, or bookTicker
      // Find @ position to determine stream type
      // Typical: {"stream":"btcusdt@depth"  or  {"stream":"btcusdt@aggTrade"
      constexpr auto kPositionStart = 15;
      const auto at_pos = payload.find('@', kPositionStart);
      if (at_pos != std::string_view::npos) [[likely]] {
        const char stream_type = payload[at_pos + 1];
        if (stream_type == 'd') {
          // @depth
          return decode_or_log<DepthResponse, "[DepthStream]">(payload);
        }
        if (stream_type == 'a') {
          // @aggTrade
          return decode_or_log<TradeEvent, "[TradeStream]">(payload);
        }
        if (stream_type == 'b') {
          // @bookTicker
          return decode_or_log<BookTickerEvent, "[BookTicker]">(payload);
        }
      }
    } else if (first_chr == 'i') {
      // {"id":"snapshot_...
      return decode_or_log<DepthSnapshot, "[DepthSnapshot]">(payload);
    } else if (first_chr == 't') {
      // {"timezone":"UTC"... - exchange info HTTP response
      return decode_or_log<ExchangeInfoResponse, "[ExchangeInfo]">(payload);
    }

    if (auto api_response = glz::read_json<ApiResponse>(payload)) {
      return WireMessage{std::in_place_type<ApiResponse>,
          std::move(*api_response)};
    }

    return WireMessage{};
  }

 private:
  struct StreamHeader {
    std::string stream;

    // clang-format off
    // NOLINTNEXTLINE(readability-identifier-naming)
    struct glaze {
      using T = StreamHeader;
      static constexpr auto value = glz::object("stream", &T::stream);  // NOLINT(readability-identifier-naming)
    };
    // clang-format on
  };

  template <class T, FixedString Label>
  [[nodiscard]] WireMessage decode_or_log(std::string_view payload) const {
    auto parsed = glz::read_json<T>(payload);
    if (!parsed) {
      auto error_msg = glz::format_error(parsed.error(), payload);
      LOG_ERROR(logger_,
          "\x1b[31m Failed to decode {} response: "
          "{}. payload:{} \x1b[0m",
          Label.view(),
          error_msg,
          payload);
      return WireMessage{};
    }
    return WireMessage{std::in_place_type<T>, std::move(*parsed)};
  }

  const common::Logger::Producer& logger_;
};

}  // namespace core

#endif  // JSON_BINANCE_FUTURES_MD_DECODER_H
