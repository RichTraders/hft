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

#ifndef SIGNATURE_H
#define SIGNATURE_H

#include <string>
#include <utility>
#include <vector>

#include <openssl/err.h>
#include <openssl/evp.h>

namespace core {
constexpr char SOH = '\x01';  // NOLINT(readability-identifier-naming)

class Util {
 public:
  static EVP_PKEY* load_ed25519(const std::string& pem, const char* password);
  static void free_key(EVP_PKEY* private_key);
  static EVP_PKEY* load_public_ed25519(const char* pem);
  static std::string sign_and_base64(EVP_PKEY* private_key,
                                     const std::string& payload);
  static int verify(const std::string& payload, const std::string& signature,
                    EVP_PKEY* public_key);
  static std::string build_canonical_query(
      std::vector<std::pair<std::string, std::string>> params);
};
}  // namespace core
#endif  //SIGNATURE_H
