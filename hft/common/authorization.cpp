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

#include "authorization.h"
#include "ini_config.hpp"

namespace common {
Authorization::Authorization() {
  IniConfig config;
#ifdef TEST_NET
  config.load("resources/test_config.ini");
#else
  config.load("resources/config.ini");
#endif

  md_address_ = config.get("auth", "md_address");
  oe_address_ = config.get("auth", "oe_address");
  port_ = config.get_int("auth", "port");
  api_key_ = config.get("auth", "api_key");
  pem_file_path_ = config.get("auth", "pem_file_path");
  private_password_ = config.get("auth", "private_password");
}

std::string Authorization::get_md_address() const {
  return md_address_;
}

std::string Authorization::get_od_address() const {
  return oe_address_;
}

int Authorization::get_port() const {
  return port_;
}

std::string Authorization::get_api_key() const {
  return api_key_;
}

std::string Authorization::get_pem_file_path() const {
  return pem_file_path_;
}

std::string Authorization::get_private_password() const {
  return private_password_;
}

}  // namespace common