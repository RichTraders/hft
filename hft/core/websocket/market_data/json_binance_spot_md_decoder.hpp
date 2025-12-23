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

#ifndef JSON_BINANCE_SPOT_MD_DECODER_H
#define JSON_BINANCE_SPOT_MD_DECODER_H

#include <common/logger.h>
#include <glaze/glaze.hpp>

#include "global.h"
#include "schema/spot/response/api_response.h"
#include "schema/spot/response/depth_stream.h"
#include "schema/spot/response/exchange_info_response.h"
#include "schema/spot/response/snapshot.h"
#include "schema/spot/response/trade.h"

namespace core {

class JsonBinanceSpotMdDecoder {
 public:
  using DepthResponse = schema::DepthResponse;
  using TradeEvent = schema::TradeEvent;
  using DepthSnapshot = schema::DepthSnapshot;
  using ApiResponse = schema::ApiResponse;
  using ExchangeInfoResponse = schema::ExchangeInfoResponse;

  using WireMessage = std::variant<std::monostate, DepthResponse, DepthSnapshot,
      TradeEvent, ApiResponse, ExchangeInfoResponse>;

  static constexpr std::string_view protocol_name() { return "JSON"; }
  static constexpr bool requires_api_key() { return false; }

  explicit JsonBinanceSpotMdDecoder(const common::Logger::Producer& logger)
      : logger_(logger) {}

  [[nodiscard]] WireMessage decode(std::string_view payload) const {
    if (payload.empty() || payload == "__CONNECTED__") {
      return WireMessage{};
    }

    if (payload.contains("snapshot")) {
      return decode_or_log<DepthSnapshot, "[DepthSnapshot]">(payload);
    }

    // Check for exchange info response
    if (payload.contains("exchangeInfo")) {
      ExchangeInfoResponse exchange;
      auto error_code =
          glz::read<glz::opts{.error_on_unknown_keys = 0}>(exchange, payload);

      if (error_code != glz::error_code::none) {
        auto msg = glz::format_error(error_code, payload);
        logger_.error(std::format("Failed to [ExchangeInfo] payload:{}. msg:{}",
            payload,
            msg));
        return WireMessage{};
      }
      return WireMessage{std::in_place_type<ExchangeInfoResponse>, exchange};
    }

    StreamHeader header_val;
    auto header_error =
        glz::read<glz::opts{.error_on_unknown_keys = 0}>(header_val, payload);
    if (header_error) {
      if (auto api_response = glz::read_json<ApiResponse>(payload)) {
        return WireMessage{std::in_place_type<ApiResponse>,
            std::move(*api_response)};
      }

      constexpr int kDefaultMinMessageLen = 100;
      logger_.warn(std::format("Unhandled websocket payload: {}",
          payload.substr(0,
              std::min<size_t>(payload.size(), kDefaultMinMessageLen))));
      return WireMessage{};
    }

    const auto& stream = header_val.stream;

    if (stream.ends_with("@depth@100ms")) {
      return decode_or_log<DepthResponse, "[DepthStream]">(payload);
    }

    if (stream.ends_with("@trade")) {
      return decode_or_log<TradeEvent, "[TradeStream]">(payload);
    }

    if (auto api_response = glz::read_json<ApiResponse>(payload)) {
      return WireMessage{std::in_place_type<ApiResponse>,
          std::move(*api_response)};
    }

    constexpr int kDefaultMinMessageLen = 100;
    logger_.warn(std::format("Unknown stream type '{}' payload: {}",
        stream,
        payload.substr(0,
            std::min<size_t>(payload.size(), kDefaultMinMessageLen))));
    return WireMessage{};
  }

 private:
  struct StreamHeader {
    std::string stream;
    // clang-format off
    // NOLINTNEXTLINE(readability-identifier-naming)
    struct glaze {
      using T = StreamHeader;
      static constexpr auto value = glz::object("stream", &T::stream); // NOLINT(readability-identifier-naming)
    };
    // clang-format on
  };

  template <class T, FixedString Label>
  [[nodiscard]] WireMessage decode_or_log(std::string_view payload) const {
    auto parsed = glz::read_json<T>(payload);
    if (!parsed) {
      auto error_msg = glz::format_error(parsed.error(), payload);
      logger_.error(
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

#endif  // JSON_BINANCE_SPOT_MD_DECODER_H
