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

#include <glaze/glaze.hpp>
#include "decoder_policy.h"

namespace core {

namespace {
template <class T>
JsonDecoderPolicy::WireMessage decode_or_log(std::string_view payload,
    std::string_view label, const common::Logger::Producer& logger) {

  auto parsed = glz::read_json<T>(payload);
  if (UNLIKELY(!parsed)) {
    auto error_msg = glz::format_error(parsed.error(), payload);
    logger.error(
        std::format("\x1b[31m Failed to decode {} response: "
                    "{}. payload:{} \x1b[0m",
            label,
            error_msg,
            payload));
    return JsonDecoderPolicy::WireMessage{};
  }
  return JsonDecoderPolicy::WireMessage{std::in_place_type<T>,
      std::move(*parsed)};
}

}  // anonymous namespace

JsonDecoderPolicy::WireMessage JsonDecoderPolicy::decode(
    std::string_view payload, const common::Logger::Producer& logger) {
  if (payload.empty()) {
    return WireMessage{};
  }
  if (payload == "__CONNECTED__") {
    return WireMessage{};
  }

  if (payload.find("@depth") != std::string_view::npos) {
    return decode_or_log<schema::DepthResponse>(payload,
        "[DepthStream]",
        logger);
  }

  if (payload.find("@trade") != std::string_view::npos) {
    return decode_or_log<schema::TradeEvent>(payload, "[TradeStream]", logger);
  }

  if (payload.find("snapshot") != std::string_view::npos) {
    return decode_or_log<schema::DepthSnapshot>(payload,
        "[DepthSnapshot]",
        logger);
  }

  if (payload.find("exchangeInfo") != std::string_view::npos) {
    schema::ExchangeInfoResponse exchange;
    auto error_code =
        glz::read<glz::opts{.error_on_unknown_keys = 0}>(exchange, payload);

    if (error_code != glz::error_code::none) {
      const std::string_view view{payload.data(), payload.size()};
      auto msg = glz::format_error(error_code, view);
      logger.error(std::format("Failed to [ExchangeInfo] payload:{}. msg:{}",
          payload,
          msg));
      return WireMessage{};
    }
    return WireMessage{std::in_place_type<schema::ExchangeInfoResponse>,
        exchange};
  }

  auto api_response = glz::read_json<schema::ApiResponse>(payload);
  if (api_response) {
    return WireMessage{std::in_place_type<schema::ApiResponse>,
        std::move(*api_response)};
  }

  constexpr int kDefaultMinMessageLen = 100;
  logger.warn(std::format("Unhandled websocket payload: {}",
      payload.substr(0,
          std::min<size_t>(payload.size(), kDefaultMinMessageLen))));
  return WireMessage{};
}

}  // namespace core
