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

#ifndef MARKET_CONSUMER_H
#define MARKET_CONSUMER_H

namespace core {
class FixApp;
}

namespace FIX8 {  // NOLINT(readability-identifier-naming)
class Message;
}

class TradeEngine;

class MarketConsumer {
 public:
  MarketConsumer();
  ~MarketConsumer();

  void on_login(FIX8::Message*);
  void on_subscribe(FIX8::Message* msg);
  void on_reject(FIX8::Message*);
  void on_logout(FIX8::Message*);
  void on_heartbeat(FIX8::Message* msg);

 private:
  std::unique_ptr<core::FixApp> app_;
  std::unique_ptr<TradeEngine> trade_engine_;
};

#endif  //MARKET_CONSUMER_H