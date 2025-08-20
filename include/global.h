/*
* MIT License
 *
 * Copyright (c) 2025 NewOro Corporation
 *
 * Permission is hereby granted, free of charge, to use, copy, modify, and
 * distribute this software for any purpose with or without fee, provided that
 * the above copyright notice appears in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 */

#pragma once

template <std::size_t Size>
struct FixedString {
  // NOLINTBEGIN(modernize-avoid-c-arrays)

  char name[Size];  // NOLINT(modernize-avoid-c-arrays)

  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr FixedString(const char (&str)[Size]) {
    std::copy_n(str, Size, name);
  }

  // NOLINTEND(modernize-avoid-c-arrays)

  constexpr bool operator==(const FixedString&) const = default;

  // NOLINTNEXTLINE(modernize-use-nodiscard)
  constexpr const char* c_str() const { return name; }
  // NOLINTNEXTLINE(modernize-use-nodiscard)
  constexpr std::size_t size() const { return Size > 0 ? Size - 1 : 0; }
  // NOLINTNEXTLINE(modernize-use-nodiscard)
  std::string_view view() const { return std::string_view(name, size()); }
  // NOLINTNEXTLINE(modernize-use-nodiscard)
  std::string str() const { return std::string(name, size()); }
};

template <std::size_t Size>
inline std::string operator+(const std::string& lhs,
                             const FixedString<Size>& rhs) {
  std::string out;
  out.reserve(lhs.size() + rhs.size());
  out += lhs;
  out.append(rhs.c_str(), rhs.size());
  return out;
}

template <std::size_t Size>
inline std::string operator+(const FixedString<Size>& lhs,
                             const std::string& rhs) {
  std::string out;
  out.reserve(lhs.size() + rhs.size());
  out.append(lhs.c_str(), lhs.size());
  out += rhs;
  return out;
}

template <std::size_t Size>
inline std::string operator+(const char* lhs, const FixedString<Size>& rhs) {
  std::string out(lhs);
  out.append(rhs.c_str(), rhs.size());
  return out;
}

template <std::size_t Size>
inline std::string operator+(const FixedString<Size>& lhs, const char* rhs) {
  std::string out(lhs.c_str(), lhs.size());
  out += rhs;
  return out;
}

template <std::size_t First, std::size_t Second>
inline std::string operator+(const FixedString<First>& lhs,
                             const FixedString<Second>& rhs) {
  std::string out;
  out.reserve(lhs.size() + rhs.size());
  out.append(lhs.c_str(), lhs.size());
  out.append(rhs.c_str(), rhs.size());
  return out;
}

template <std::size_t Size>
inline std::ostream& operator<<(std::ostream& ost,
                                const FixedString<Size>& fix_str) {
  return ost.write(fix_str.c_str(),
                   static_cast<std::streamsize>(fix_str.size()));
}