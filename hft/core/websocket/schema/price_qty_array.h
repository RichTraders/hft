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

#ifndef PRICE_QTY_ARRAY_H
#define PRICE_QTY_ARRAY_H

#include <glaze/glaze.hpp>

namespace schema {
struct PriceQtyArray {
  std::vector<std::array<double, 2>> data;
  PriceQtyArray() = default;
  explicit PriceQtyArray(std::vector<std::array<double, 2>>&& d)
      : data(std::move(d)) {}
  [[nodiscard]] auto begin() const { return data.begin(); }
  [[nodiscard]] auto end() const { return data.end(); }
  [[nodiscard]] size_t size() const { return data.size(); }
  [[nodiscard]] bool empty() const { return data.empty(); }
  const std::array<double, 2>& operator[](size_t i) const { return data[i]; }
};

}  
template <>
struct glz::meta<::schema::PriceQtyArray> {
  static constexpr auto custom_read = true;
  static constexpr auto custom_write = true;
};
template <>
struct glz::detail::from<glz::JSON, ::schema::PriceQtyArray> {
  template <glz::opts Opts, typename It, typename End>
  static void op(::schema::PriceQtyArray& value, glz::is_context auto&& /*ctx*/,
      It&& it, End&& end) noexcept {
    value.data.clear();
    
    while (it != end &&
           (*it == ' ' || *it == '\t' || *it == '\n' || *it == '\r')) {
      ++it;
    }
    if (it == end || *it != '[') {
      return;
    }
    ++it;  
    
    value.data.reserve(1024);
    while (it != end) {
      
      while (it != end &&
             (*it == ' ' || *it == '\t' || *it == '\n' || *it == '\r')) {
        ++it;
      }
      if (it == end)
        break;
      if (*it == ']') {
        ++it;
        break;
      }
      if (*it == ',') {
        ++it;
        continue;
      }
      
      if (*it != '[') {
        break;
      }
      ++it;
      std::array<double, 2> entry{};
      
      while (it != end && *it != '"')
        ++it;
      if (it == end)
        break;
      ++it;  
      const char* price_start = &(*it);
      while (it != end && *it != '"')
        ++it;
      if (it == end)
        break;
      const char* price_end = &(*it);
      std::from_chars(price_start, price_end, entry[0]);
      ++it;  
      
      while (it != end && *it != ',')
        ++it;
      if (it != end)
        ++it;
      
      while (it != end && *it != '"')
        ++it;
      if (it == end)
        break;
      ++it;  
      const char* qty_start = &(*it);
      while (it != end && *it != '"')
        ++it;
      if (it == end)
        break;
      const char* qty_end = &(*it);
      std::from_chars(qty_start, qty_end, entry[1]);
      ++it;  
      
      while (it != end && *it != ']')
        ++it;
      if (it != end)
        ++it;
      value.data.push_back(entry);
    }
  }
};

template <>
struct glz::detail::to<glz::JSON, ::schema::PriceQtyArray> {
  template <glz::opts Opts>
  static void op(const ::schema::PriceQtyArray& value,
      glz::is_context auto&& /*ctx*/, auto&&... args) noexcept {
    
    glz::detail::dump<'['>(args...);
    bool first = true;
    for (const auto& entry : value.data) {
      if (!first) {
        glz::detail::dump<','>(args...);
      }
      first = false;
      glz::detail::dump<'['>(args...);
      glz::detail::dump<'"'>(args...);
      glz::detail::write_chars::op<Opts>(entry[0], args...);
      glz::detail::dump<'"'>(args...);
      glz::detail::dump<','>(args...);
      glz::detail::dump<'"'>(args...);
      glz::detail::write_chars::op<Opts>(entry[1], args...);
      glz::detail::dump<'"'>(args...);
      glz::detail::dump<']'>(args...);
    }
    glz::detail::dump<']'>(args...);
  }
};

#endif  // PRICE_QTY_ARRAY_H
