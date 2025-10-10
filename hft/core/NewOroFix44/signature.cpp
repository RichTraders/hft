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

#include "signature.h"
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <pch.h>

constexpr int kBaseSize = 64;

namespace core {
int password_cb(char* buf, int size, int, void* userdata) {
  const auto* password = static_cast<const char*>(userdata);
  const auto len = std::min<>(static_cast<int>(strlen(password)), size - 1);
  memcpy(buf, password, len);
  buf[len] = '\0';
  return len;
}

// NOLINT(bugprone-easily-swappable-parameters,-warnings-as-errors)
EVP_PKEY* Util::load_ed25519(const std::string& pem, const char* password)
// PEM → EVP_PKEY*
{
  FILE* file = fopen(pem.data(), "r");
  if (!file)
    throw std::runtime_error("key open fail");
  EVP_PKEY* private_key = PEM_read_PrivateKey(file, nullptr, password_cb,
                                              const_cast<char*>(password));
  fclose(file);
  return private_key;
}

void Util::free_key(EVP_PKEY* private_key) {
  EVP_PKEY_free(private_key);
}

EVP_PKEY* Util::load_public_ed25519(const char* pem) {
  FILE* pub_fp = fopen(pem, "r");
  EVP_PKEY* pubkey = PEM_read_PUBKEY(pub_fp, nullptr, nullptr, nullptr);
  fclose(pub_fp);
  return pubkey;
}

// TODO(jb): support RSA signature
std::string Util::sign_and_base64(EVP_PKEY* private_key,
                                  const std::string& payload) {
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, private_key);

  size_t siglen = kBaseSize;
  EVP_DigestSign(ctx, nullptr, &siglen,
                 reinterpret_cast<const unsigned char*>(payload.data()),
                 payload.size());

  std::vector<unsigned char> sig(siglen);
  EVP_DigestSign(ctx, sig.data(), &siglen,
                 reinterpret_cast<const unsigned char*>(payload.data()),
                 payload.size());

  sig.resize(siglen);
  EVP_MD_CTX_free(ctx);

  BIO* b64 = BIO_new(BIO_f_base64());
  BIO* mem = BIO_new(BIO_s_mem());
  BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);  // 줄바꿈 제거
  BIO* bio = BIO_push(b64, mem);
  BIO_write(bio, sig.data(), static_cast<int>(sig.size()));
  BIO_flush(bio);

  BUF_MEM* bptr;
  BIO_get_mem_ptr(bio, &bptr);
  std::string out(bptr->data, bptr->length);
  BIO_free_all(bio);
  return out;
}

int Util::verify(const std::string& payload, const std::string& signature,
                 EVP_PKEY* public_key) {
  std::vector<unsigned char> sig_bin;  // 64 bytes
  {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* mem =
        BIO_new_mem_buf(signature.data(), static_cast<int>(signature.size()));
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO* bio = BIO_push(b64, mem);

    sig_bin.resize(kBaseSize);
    const int read = BIO_read(bio, sig_bin.data(), kBaseSize);
    sig_bin.resize(read);  // 정상이라면 n == 64
    BIO_free_all(bio);
  }

  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, public_key);

  const int verified = EVP_DigestVerify(
      ctx, sig_bin.data(), sig_bin.size(),  // ← 바이너리 서명
      reinterpret_cast<const unsigned char*>(payload.data()), payload.size());
  EVP_MD_CTX_free(ctx);
  return verified;
}
}  // namespace core