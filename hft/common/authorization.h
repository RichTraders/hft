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

#ifndef AUTHORIZATION_H
#define AUTHORIZATION_H

struct Authorization {
  std::string md_address;
  std::string oe_address;
  int port;
  std::string api_key;
  std::string pem_file_path;
  std::string private_password;
};

#endif  //AUTHORIZATION_H
