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

#ifndef WS_OE_DECODER_BASE_H
#define WS_OE_DECODER_BASE_H

#include <glaze/glaze.hpp>
#include "common/logger.h"
#include "global.h"

namespace core {

template <typename Derived>
class WsOeDecoderBase {
 protected:
  template <class T, FixedString Label>
  [[nodiscard]] auto decode_or_log(std::string_view payload) const {
    using WireMessage = typename Derived::WireMessage;

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

 public:
  explicit WsOeDecoderBase(const common::Logger::Producer& logger)
      : logger_(logger) {}

  [[nodiscard]] auto decode(std::string_view payload) const {
    return static_cast<const Derived*>(this)->decode_impl(payload);
  }
};

}  // namespace core

#endif  // WS_OE_DECODER_BASE_H
