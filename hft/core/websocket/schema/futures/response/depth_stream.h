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

#ifndef FUTURES_DEPTH_H
#define FUTURES_DEPTH_H
#include <glaze/glaze.hpp>
namespace schema::futures {
struct DepthData {
  std::string event_type;
  std::uint64_t timestamp;
  std::uint64_t transaction_time;
  std::string symbol;
  std::uint64_t start_update_id;
  std::uint64_t end_update_id;
  std::uint64_t final_update_id_in_last_stream;

  std::vector<std::array<double, 2>> bids;
  std::vector<std::array<double, 2>> asks;

  // clang-format off
  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = DepthData;
    static constexpr auto value =  // NOLINT(readability-identifier-naming)  // NOLINT(readability-identifier-naming)
      glz::object(
        "e", &T::event_type,
        "E", &T::timestamp,
        "T", &T::transaction_time,
        "s", &T::symbol,
        "U", &T::start_update_id,
        "u", &T::end_update_id,
        "pu", &T::final_update_id_in_last_stream,
        "b", glz::quoted_num<&T::bids>,
        "a", glz::quoted_num<&T::asks>);
  };
  // clang-format on
};

struct DepthResponse {
  std::string stream;
  DepthData data;

  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = DepthResponse;
    // NOLINTNEXTLINE(readability-identifier-naming)
    static constexpr auto value =  // NOLINT(readability-identifier-naming)
        glz::object("stream", &T::stream, "data", &T::data);
  };
};
}  // namespace schema::futures

#endif  //FUTURES_DEPTH_H
