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

#ifndef WS_MD_DECODER_H
#define WS_MD_DECODER_H

#include "common/logger.h"
#include "ws_md_wire_message.h"

#include "schema/response/depth_stream.h"
#include "schema/response/exchange_info_response.h"
#include "schema/response/snapshot.h"
#include "schema/response/trade.h"
namespace core {
class WsMdDecoder {
 public:
  using WireMessage = WsMdWireMessage;
  using RequestId = std::string;
  using MarketDepthLevel = std::string;
  using SymbolId = std::string;

  explicit WsMdDecoder(const common::Logger::Producer& logger)
      : logger_(logger) {}

  [[nodiscard]] WireMessage decode(std::string_view payload) const;

 private:
  template <class T>
  [[nodiscard]] WireMessage decode_or_log(std::string_view payload,
      std::string_view label) const;

  const common::Logger::Producer& logger_;
};
}  // namespace core

#endif  //WS_MD_DECODER_H
