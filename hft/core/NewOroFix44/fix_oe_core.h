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

#pragma once
#include <pch.h>
#include "logger.h"
#include "memory_pool.hpp"
#include "order_entry.h"

namespace FIX8 {
class Message;
class MessageBase;
namespace NewOroFix44OE {
class ExecutionReport;
}
}

struct OrderData {

};

namespace core {
class FixOeCore {
public:
  using SendId = std::string;
  using TargetId = std::string;

  //DEPTH_STREAM, BOOK_TICKER_STREAM, TRADE_STREAM
  using RequestId = std::string;
  using MarketDepthLevel = std::string;
  using SymbolId = std::string;

  FixOeCore(SendId sender_comp_id,
      TargetId target_comp_id,
      common::Logger* logger);
  std::string create_log_on_message(
      const std::string& sig_b64, const std::string& timestamp);
  std::string create_log_out_message();
  std::string create_heartbeat_message(FIX8::Message* message);
  std::string create_order_message(const trading::NewSingleOrderData& order_data);
  trading::ExecutionReport create_excution_report_message(FIX8::NewOroFix44OE::ExecutionReport* msg);
  FIX8::Message* decode(const std::string& message);

private:
  int64_t sequence_{1};
  const std::string sender_comp_id_;
  const std::string target_comp_id_;
  common::Logger* logger_;
};
}