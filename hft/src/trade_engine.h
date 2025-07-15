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

#ifndef TRADE_ENGINE_H
#define TRADE_ENGINE_H

#include "common/spsc_queue.h"

class TradeEngine {
 public:
  TradeEngine();
  ~TradeEngine();

  void push();

 private:
  std::unique_ptr<common::SPSCQueue<int>> queue_;
};

#endif  //TRADE_ENGINE_H