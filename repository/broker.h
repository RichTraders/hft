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

#ifndef BROKER_H
#define BROKER_H

#include "core/NewOroFix44/fix_app.h"

class Broker {
 public:
  Broker();

 private:
  std::unique_ptr<core::FixApp<1>> app_;

  void on_login(FIX8::Message*) const;
  void on_heartbeat(FIX8::Message*) const;
  static void on_subscribe(const std::string& msg);
};

#endif  //BROKER_H