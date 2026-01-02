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

#ifndef PROTOCOL_DECODER_H
#define PROTOCOL_DECODER_H

#include <concepts>
#include <string_view>

template <typename T>
concept ProtocolDecoder = requires(const T& decoder, std::string_view payload) {
  typename T::WireMessage;

  { decoder.decode(payload) } -> std::convertible_to<typename T::WireMessage>;
  { T::requires_api_key() } -> std::same_as<bool>;
  { T::protocol_name() } -> std::convertible_to<std::string_view>;
};

#endif  // PROTOCOL_DECODER_H