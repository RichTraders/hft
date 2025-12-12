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

#ifndef FUTURES_SESSION_H
#define FUTURES_SESSION_H

#include <glaze/glaze.hpp>

namespace schema::futures {

struct UserDataStreamStartRequest {
  std::string id;
  std::string method = "userDataStream.start";
  struct Params {
    std::string apiKey;
  } params;

  struct glaze {
    using T = UserDataStreamStartRequest;
    static constexpr auto value =
        glz::object("id", &T::id, "method", &T::method, "params", &T::params);
  };
};

struct UserDataStreamPingRequest {
  std::string id;
  std::string method = "userDataStream.ping";
  struct Params {
    std::string apiKey;
  } params;

  struct glaze {
    using T = UserDataStreamPingRequest;
    static constexpr auto value =
        glz::object("id", &T::id, "method", &T::method, "params", &T::params);
  };
};

struct UserDataStreamStopRequest {
  std::string id;
  std::string method = "userDataStream.stop";
  struct Params {
    std::string apiKey;
  } params;

  struct glaze {
    using T = UserDataStreamStopRequest;
    static constexpr auto value =
        glz::object("id", &T::id, "method", &T::method, "params", &T::params);
  };
};

}  // namespace schema::futures

template <>
struct glz::meta<schema::futures::UserDataStreamStartRequest::Params> {
  using T = schema::futures::UserDataStreamStartRequest::Params;
  static constexpr auto value = object("apiKey", &T::apiKey);
};

template <>
struct glz::meta<schema::futures::UserDataStreamPingRequest::Params> {
  using T = schema::futures::UserDataStreamPingRequest::Params;
  static constexpr auto value = object("apiKey", &T::apiKey);
};

template <>
struct glz::meta<schema::futures::UserDataStreamStopRequest::Params> {
  using T = schema::futures::UserDataStreamStopRequest::Params;
  static constexpr auto value = object("apiKey", &T::apiKey);
};

#endif  // FUTURES_SESSION_H
