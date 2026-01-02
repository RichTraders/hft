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

#ifndef LISTEN_KEY_RESPONSE_H
#define LISTEN_KEY_RESPONSE_H

#include "pch.h"

namespace schema::futures {

struct ListenKeyResponse {
  std::string listenKey;
};

}  // namespace schema::futures

template <>
struct glz::meta<schema::futures::ListenKeyResponse> {
  using T = schema::futures::ListenKeyResponse;
  static constexpr auto value = object("listenKey", &T::listenKey);  // NOLINT(readability-identifier-naming)  // NOLINT(readability-identifier-naming)
};

#endif  // LISTEN_KEY_RESPONSE_H
