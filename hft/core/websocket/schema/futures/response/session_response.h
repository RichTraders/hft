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

#ifndef FUTURES_SESSION_RESPONSE_H
#define FUTURES_SESSION_RESPONSE_H

#include <glaze/glaze.hpp>
namespace schema {
namespace futures {
struct SessionLogonResponse {
  std::string listen_key;
  struct glaze {
    using T = SessionLogonResponse;
    static constexpr auto value = glz::object("listenKey", &T::listen_key);
  };
};
}  // namespace futures
}  // namespace schema

#endif  //FUTURES_SESSION_RESPONSE_H
