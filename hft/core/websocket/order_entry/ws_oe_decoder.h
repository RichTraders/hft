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

#ifndef WS_OE_DECODER_H
#define WS_OE_DECODER_H

#include "common/logger.h"
#include "ws_oe_wire_message.h"

namespace core {

class WsOeDecoder {
 public:
  using WireMessage = WsOeWireMessage;

  explicit WsOeDecoder(const common::Logger::Producer& logger)
      : logger_(logger) {}

  [[nodiscard]] WireMessage decode(std::string_view payload) const;

 private:
  template <class T>
  [[nodiscard]] WireMessage decode_or_log(std::string_view payload,
      std::string_view label) const;

  const common::Logger::Producer& logger_;
};

}  // namespace core

#endif  //WS_OE_DECODER_H
