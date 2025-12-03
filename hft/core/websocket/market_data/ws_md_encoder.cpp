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

#include "ws_md_encoder.h"
#include <glaze/glaze.hpp>
#include "schema/request/exchange_info.h"

namespace {
constexpr std::string_view kDepthSuffix = "@depth@100ms";
constexpr std::string_view kTradeSuffix = "@trade";
constexpr std::string_view kSubscribe = "SUBSCRIBE";
constexpr std::string_view kUnsubscribe = "UNSUBSCRIBE";
}  // namespace
namespace core {
std::string WsMdEncoder::create_market_data_subscription_message(
    const RequestId& /*request_id*/, const MarketDepthLevel& /*level*/,
    const SymbolId& symbol, bool subscribe) const {
  if (symbol.empty()) {
    return {};
  }

  std::string stream = symbol.data();
  std::ranges::transform(stream, stream.begin(), [](unsigned char chr) {
    return static_cast<char>(std::tolower(chr));
  });
  stream.append(kDepthSuffix);
  const std::string method =
      subscribe ? std::string(kSubscribe) : std::string(kUnsubscribe);

  return std::format(R"({{"method":"{}","params":["{}"],"id":{}}})",
      method,
      stream,
      request_sequence_++);
}

std::string WsMdEncoder::create_snapshot_data_subscription_message(
    const MarketDepthLevel& level, const SymbolId& symbol) const {
  if (symbol.empty()) {
    return {};
  }
  std::string upper_symbol = symbol.data();
  std::ranges::transform(upper_symbol,
      upper_symbol.begin(),
      [](unsigned char chr) { return static_cast<char>(std::toupper(chr)); });
  return std::format(
      R"({{"id":"snapshot_{}","method":"depth","params":{{"symbol":"{}","limit":{}}}}})",
      upper_symbol,
      upper_symbol,
      level);
}

std::string WsMdEncoder::create_trade_data_subscription_message(
    const RequestId& /*request_id*/, const MarketDepthLevel& /*level*/,
    const SymbolId& symbol, bool subscribe) const {
  if (symbol.empty()) {
    return {};
  }
  std::string stream = symbol.data();
  std::ranges::transform(stream, stream.begin(), [](const unsigned char chr) {
    return static_cast<char>(std::tolower(chr));
  });
  stream.append(kTradeSuffix);
  const std::string method =
      subscribe ? std::string(kSubscribe) : std::string(kUnsubscribe);
  return std::format(R"({{"method":"{}","params":["{}"],"id":{}}})",
      method,
      stream,
      request_sequence_++);
}

std::string WsMdEncoder::request_instrument_list_message(
    const std::string& symbol) const {
  schema::ExchangeInfoRequest request;
  request.id = "md_exchangeInfo";
  request.params.symbols = {symbol};
  return glz::write_json(request).value_or(std::string{});
}
}  // namespace core