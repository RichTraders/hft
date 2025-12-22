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
  md_address_ = INI_CONFIG.get("auth", "md_address");
  oe_address_ = INI_CONFIG.get("auth", "oe_address");
  port_ = INI_CONFIG.get_int("auth", "port");
  md_ws_address_ = INI_CONFIG.get("auth", "md_ws_address", md_address_);
  md_ws_port_ = INI_CONFIG.get_int("auth", "md_ws_port", port_);
  md_ws_path_ = INI_CONFIG.get_with_symbol("auth", "md_ws_path", "/");
  md_ws_use_ssl_ = INI_CONFIG.get_int("auth", "md_ws_use_ssl", 1) != 0;
  md_ws_write_address_ =
      INI_CONFIG.get("auth", "md_ws_write_address", md_address_);
  md_ws_write_port_ = INI_CONFIG.get_int("auth", "md_ws_write_port", port_);
  md_ws_write_path_ = INI_CONFIG.get("auth", "md_ws_write_path", "/");
  md_ws_write_use_ssl_ =
      INI_CONFIG.get_int("auth", "md_ws_write_use_ssl", 1) != 0;
  oe_ws_address_ = INI_CONFIG.get("auth", "oe_ws_address", oe_address_);
  oe_ws_port_ = INI_CONFIG.get_int("auth", "oe_ws_port", port_);
  oe_ws_path_ = INI_CONFIG.get("auth", "oe_ws_path", "/");
  oe_ws_use_ssl_ = INI_CONFIG.get_int("auth", "oe_ws_use_ssl", 1) != 0;
  api_key_ = INI_CONFIG.get("auth", "api_key");
  pem_file_path_ = INI_CONFIG.get("auth", "pem_file_path");
  private_password_ = INI_CONFIG.get("auth", "private_password");
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

std::string Authorization::get_md_ws_address() const {
  return md_ws_address_;
}

int Authorization::get_md_ws_port() const {
  return md_ws_port_;
}

std::string Authorization::get_md_ws_path() const {
  return md_ws_path_;
}

bool Authorization::use_md_ws_ssl() const {
  return md_ws_use_ssl_;
}

std::string Authorization::get_oe_ws_address() const {
  return oe_ws_address_;
}

int Authorization::get_oe_ws_port() const {
  return oe_ws_port_;
}

std::string Authorization::get_oe_ws_path() const {
  return oe_ws_path_;
}

bool Authorization::use_oe_ws_ssl() const {
  return oe_ws_use_ssl_;
}

std::string Authorization::get_md_ws_write_address() const {
  return md_ws_write_address_;
}

int Authorization::get_md_ws_write_port() const {
  return md_ws_write_port_;
}

std::string Authorization::get_md_ws_write_path() const {
  return md_ws_write_path_;
}

bool Authorization::use_md_ws_write_ssl() const {
  return md_ws_write_use_ssl_;
}

std::string Authorization::get_oe_ws_write_address() const {
  return oe_ws_write_address_;
}

int Authorization::get_oe_ws_write_port() const {
  return oe_ws_write_port_;
}

std::string Authorization::get_oe_ws_write_path() const {
  return oe_ws_write_path_;
}

bool Authorization::use_oe_ws_write_ssl() const {
  return oe_ws_write_use_ssl_;
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
