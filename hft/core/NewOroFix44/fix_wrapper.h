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

#ifndef FIX_WRAPPER_H
#define FIX_WRAPPER_H

namespace FIX8 {
class Message;
}

namespace core {
class Fix {
public:
  using SendId = std::string;
  using TargetId = std::string;

  //DEPTH_STREAM, BOOK_TICKER_STREAM, TRADE_STREAM
  using RequestId = std::string;
  using MarketDepthLevel = std::string;
  using SymbolId = std::string;

  Fix(SendId sender_comp_id, TargetId target_comp_id);

  std::string create_log_on_message(
      const std::string& sig_b64, const std::string& timestamp);

  std::string create_log_out_message();
  std::string create_heartbeat_message();

  std::string create_market_data_subscription_message(const RequestId& request_id,
                                          const MarketDepthLevel& level, const SymbolId& symbol);

  std::string timestamp();

  FIX8::Message* get_data(const std::string& message);
  const std::string get_sigature_base64(const std::string& timestamp);

private:
  int64_t sequence_{1};
  const std::string sender_comp_id_;
  const std::string target_comp_id_;
};
}


#endif //FIX_WRAPPER_H