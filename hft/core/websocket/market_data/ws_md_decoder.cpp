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

#include "ws_md_decoder.h"

#include "glaze/glaze.hpp"

namespace core {

using schema::DepthResponse;
using schema::DepthSnapshot;
using schema::ExchangeInfoResponse;
using schema::TradeEvent;

WsMdDecoder::WireMessage WsMdDecoder::decode(std::string_view payload) const {
  if (payload.empty()) {
    return WireMessage{};
  }
  if (payload == "__CONNECTED__") {
    return WireMessage{};
  }

  if (payload.find("@depth") != std::string_view::npos) {
    return decode_or_log<DepthResponse>(payload, "[DepthStream]");
  }

  if (payload.find("@trade") != std::string_view::npos) {
    return decode_or_log<TradeEvent>(payload, "[TradeStream]");
  }

  if (payload.find("snapshot") != std::string_view::npos) {
    return decode_or_log<DepthSnapshot>(payload, "[DepthSnapshot]");
  }

  if (payload.find("exchangeInfo") != std::string_view::npos) {
    schema::ExchangeInfoResponse exchange;
    auto error_code =
        glz::read<glz::opts{.error_on_unknown_keys = 0}>(exchange, payload);

    if (error_code != glz::error_code::none) {
      const std::string_view view{payload.data(), payload.size()};
      auto msg = glz::format_error(error_code, view);
      logger_.error("Failed to [ExchangeInfo] payload:{}. msg:{}",
          payload,
          msg);
      return WireMessage{};
    }
    return WireMessage{std::in_place_type<schema::ExchangeInfoResponse>,
        exchange};
  }

  constexpr int kDefaultMinMessageLen = 100;
  logger_.warn("Unhandled websocket payload: {}",
      payload.substr(0,
          std::min<size_t>(payload.size(), kDefaultMinMessageLen)));
  return WireMessage{};
}

template <class T>
WsMdDecoder::WireMessage WsMdDecoder::decode_or_log(std::string_view payload,
    std::string_view label) const {
  auto parsed = glz::read_json<T>(payload);
  if (UNLIKELY(!parsed)) {
    auto error_msg = glz::format_error(parsed.error(), payload);
    logger_.error(
        "\x1b[31m Failed to decode {} response: "
        "{}. payload:{} \x1b[0m",
        label,
        error_msg,
        payload);
    return WireMessage{};
  }
  return WireMessage{std::in_place_type<T>, std::move(*parsed)};
}

}  // namespace core