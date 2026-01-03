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

#ifndef FUTURES_USERDATA_STREAM_REQUEST_H
#define FUTURES_USERDATA_STREAM_REQUEST_H

#include <glaze/glaze.hpp>

namespace schema::futures {

struct UserDataStreamStartRequest {
  std::string id;
  std::string method = "userDataStream.start";
  struct Params {
    std::string apiKey;

  // NOLINTNEXTLINE(readability-identifier-naming)
    struct glaze {
      using T = Params;
      static constexpr auto value = glz::object("apiKey", &T::apiKey);  // NOLINT(readability-identifier-naming)  // NOLINT(readability-identifier-naming)
    };
  } params;

  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = UserDataStreamStartRequest;
    static constexpr auto value =  // NOLINT(readability-identifier-naming)
        glz::object("id", &T::id, "method", &T::method, "params", &T::params);
  };
};

struct UserDataStreamPingRequest {
  std::string id;
  std::string method = "userDataStream.ping";
  struct Params {
    std::string apiKey;

  // NOLINTNEXTLINE(readability-identifier-naming)
    struct glaze {
      using T = Params;
      static constexpr auto value = glz::object("apiKey", &T::apiKey);  // NOLINT(readability-identifier-naming)  // NOLINT(readability-identifier-naming)
    };
  } params;

  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = UserDataStreamPingRequest;
    static constexpr auto value =  // NOLINT(readability-identifier-naming)
        glz::object("id", &T::id, "method", &T::method, "params", &T::params);
  };
};

struct UserDataStreamStopRequest {
  std::string id;
  std::string method = "userDataStream.stop";
  struct Params {
    std::string apiKey;

  // NOLINTNEXTLINE(readability-identifier-naming)
    struct glaze {
      using T = Params;
      static constexpr auto value = glz::object("apiKey", &T::apiKey);  // NOLINT(readability-identifier-naming)  // NOLINT(readability-identifier-naming)
    };
  } params;

  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = UserDataStreamStopRequest;
    static constexpr auto value =  // NOLINT(readability-identifier-naming)
        glz::object("id", &T::id, "method", &T::method, "params", &T::params);
  };
};

}  // namespace schema::futures

#endif  // FUTURES_USERDATA_STREAM_REQUEST_H
