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

#ifndef SESSION_H
#define SESSION_H
#include <glaze/glaze.hpp>
namespace schema {
struct SessionUserSubscriptionRequest {
  std::string id;
  const std::string method = "userDataStream.subscribe";
  struct glaze {
    using T = SessionUserSubscriptionRequest;
    static constexpr auto value =
        glz::object("id", &T::id, "method", &T::method);
  };
};

struct SessionUserUnsubscriptionRequest {
  std::string id;
  std::string method = "userDataStream.unsubscribe";
  struct glaze {
    using T = SessionUserUnsubscriptionRequest;
    static constexpr auto value =
        glz::object("id", &T::id, "method", &T::method);
  };
};
}  // namespace schema
#endif  //SESSION_H
