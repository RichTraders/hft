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

#include "ws_oe_decoder.h"

namespace core {

WsOeDecoder::WireMessage WsOeDecoder::decode(std::string_view payload) const {
  if (payload.empty()) {
    return WireMessage{};
  }
  logger_.info(std::format("[WsOeCore]payload :{}", payload));

  if (payload.find("executionReport") != std::string_view::npos) {
    return decode_or_log<schema::ExecutionReportResponse>(payload,
        "[executionReport]");
  }
  if (payload.find("outboundAccountPosition") != std::string_view::npos) {
    return decode_or_log<schema::OutboundAccountPositionEnvelope>(payload,
        "[outboundAccountPosition]");
  }
  if (payload.find("balanceUpdate") != std::string_view::npos) {
    return decode_or_log<schema::BalanceUpdateEnvelope>(payload,
        "[balanceUpdate]");
  }

  schema::WsHeader header{};
  const auto error_code =
      glz::read<glz::opts{.error_on_unknown_keys = 0, .partial_read = 1}>(
          header,
          payload);
  if (error_code != glz::error_code::none) {
    logger_.error(std::format("Failed to decode payload"));
    return WireMessage{};
  }
  logger_.debug(std::format("[WsOeCore]header id :{}", header.id));

  if (header.id.starts_with("login_")) {
    return decode_or_log<schema::SessionLogonResponse>(payload,
        "[session.logon]");
  }

  if (header.id.starts_with("subscribe")) {
    return decode_or_log<schema::SessionUserSubscriptionResponse>(payload,
        "[userDataStream.subscribe]");
  }

  if (header.id.starts_with("unsubscribe")) {
    return decode_or_log<schema::SessionUserUnsubscriptionResponse>(payload,
        "[userDataStream.unsubscribe]");
  }

  if (header.id.starts_with("order")) {
    if (header.id.starts_with("orderreplace")) {
      return decode_or_log<schema::CancelAndReorderResponse>(payload,
          "[cancelReplace]");
    }
    if (header.id.starts_with("ordercancelAll")) {
      return decode_or_log<schema::CancelAllOrdersResponse>(payload,
          "[cancelAll]");
    }
    if (header.id.starts_with("ordercancel")) {
      return decode_or_log<schema::CancelOrderResponse>(payload,
          "[orderCancel]");
    }
    return decode_or_log<schema::PlaceOrderResponse>(payload, "[orderPlace]");
  }

  return decode_or_log<schema::ApiResponse>(payload, "[API response]");
}

template <class T>
WsOeDecoder::WireMessage WsOeDecoder::decode_or_log(std::string_view payload,
    std::string_view label) const {
  auto parsed = glz::read_json<T>(payload);
  if (!parsed) {
    auto error_msg = glz::format_error(parsed.error(), payload);
    logger_.error(
        std::format("\x1b[31m Failed to decode {} response: "
                    "{}. payload:{} \x1b[0m",
            label,
            error_msg,
            payload));
    return WireMessage{};
  }
  return WireMessage{std::in_place_type<T>, std::move(*parsed)};
}
}  // namespace core
