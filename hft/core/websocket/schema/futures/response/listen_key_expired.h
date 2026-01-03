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

#ifndef LISTEN_KEY_EXPIRED_H
#define LISTEN_KEY_EXPIRED_H

#include <glaze/glaze.hpp>

namespace schema::futures {

struct ListenKeyExpiredEvent {
  std::string event_type;
  std::uint64_t event_time{};
  std::string listen_key;

  // clang-format off
  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = ListenKeyExpiredEvent;
    static constexpr auto value = glz::object(  // NOLINT(readability-identifier-naming)
      "e", &T::event_type,
      "E", glz::quoted_num<&T::event_time>,
      "listenKey", &T::listen_key
    );
  };
  // clang-format on
};

}  // namespace schema::futures

#endif  // LISTEN_KEY_EXPIRED_H
