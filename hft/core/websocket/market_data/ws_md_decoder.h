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
#include "decoder_policy.h"

namespace core {

template <DecoderPolicy Policy>
class WsMdDecoder {
 public:
  using WireMessage = typename Policy::WireMessage;
  using RequestId = std::string;
  using MarketDepthLevel = std::string;
  using SymbolId = std::string;

  explicit WsMdDecoder(const common::Logger::Producer& logger)
      : logger_(logger) {}

  [[nodiscard]] WireMessage decode(std::string_view payload) const {
    return Policy::decode(payload, logger_);
  }

 private:
  const common::Logger::Producer& logger_;
};

}  // namespace core

#endif  //WS_MD_DECODER_H
