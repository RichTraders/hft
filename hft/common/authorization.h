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

#ifndef COMMON_AUTHROIZATION_H
#define COMMON_AUTHROIZATION_H

#include <string>

#include "singleton.h"

namespace common {
class Authorization : public Singleton<Authorization> {
 public:
  Authorization();
  [[nodiscard]] std::string get_md_address() const;
  [[nodiscard]] std::string get_od_address() const;
  [[nodiscard]] int get_port() const;

  [[nodiscard]] std::string get_md_ws_address() const;
  [[nodiscard]] int get_md_ws_port() const;
  [[nodiscard]] std::string get_md_ws_path() const;
  [[nodiscard]] bool use_md_ws_ssl() const;
  [[nodiscard]] std::string get_oe_ws_address() const;
  [[nodiscard]] int get_oe_ws_port() const;
  [[nodiscard]] std::string get_oe_ws_path() const;
  [[nodiscard]] bool use_oe_ws_ssl() const;

  [[nodiscard]] std::string get_md_ws_write_address() const;
  [[nodiscard]] int get_md_ws_write_port() const;
  [[nodiscard]] std::string get_md_ws_write_path() const;
  [[nodiscard]] bool use_md_ws_write_ssl() const;
  [[nodiscard]] std::string get_oe_ws_write_address() const;
  [[nodiscard]] int get_oe_ws_write_port() const;
  [[nodiscard]] std::string get_oe_ws_write_path() const;
  [[nodiscard]] bool use_oe_ws_write_ssl() const;

  [[nodiscard]] std::string get_api_key() const;
  [[nodiscard]] std::string get_pem_file_path() const;
  [[nodiscard]] std::string get_private_password() const;

 private:
  std::string md_address_;
  std::string oe_address_;
  int port_;

  std::string md_ws_address_;
  int md_ws_port_;
  std::string md_ws_path_;
  bool md_ws_use_ssl_;
  std::string oe_ws_address_;
  int oe_ws_port_;
  std::string oe_ws_path_;
  bool oe_ws_use_ssl_;

  std::string md_ws_write_address_;
  int md_ws_write_port_;
  std::string md_ws_write_path_;
  bool md_ws_write_use_ssl_;
  std::string oe_ws_write_address_;
  int oe_ws_write_port_;
  std::string oe_ws_write_path_;
  bool oe_ws_write_use_ssl_;

  std::string api_key_;
  std::string pem_file_path_;
  std::string private_password_;
};
}  // namespace common

#define AUTHORIZATION common::Authorization::instance()

#endif