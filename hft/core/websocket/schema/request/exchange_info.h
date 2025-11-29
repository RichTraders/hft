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

#ifndef EXCHANGE_INFO_H
#define EXCHANGE_INFO_H

#include "glaze/core/common.hpp"
namespace schema {
struct Symbol {
  std::vector<std::string> symbols;
  struct glaze {
    using T = Symbol;
    static constexpr auto value = glz::object("symbols", &T::symbols);
  };
};
struct ExchangeInfoRequest {
  std::string id;
  const std::string method = "exchangeInfo";
  Symbol params;

  struct glaze {
    using T = ExchangeInfoRequest;
    static constexpr auto value =
        glz::object("id", &T::id, "method", &T::method, "params", &T::params);
  };
};
}  // namespace schema
#endif  //EXCHANGE_INFO_H
