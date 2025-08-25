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

#include "logger.h"
#include "memory_pool.hpp"
#include "order_entry.h"

namespace FIX8 {
class Message;
class MessageBase;

namespace NewOroFix44OE {
class ExecutionReport;
class OrderCancelReject;
class OrderMassCancelReport;
class Reject;
}
}

struct OrderData {};

namespace trading {
class ResponseManager;
}

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
            common::Logger* logger, trading::ResponseManager* response_manager);

  ~FixOeCore();

  std::string create_log_on_message(
      const std::string& sig_b64, const std::string& timestamp);
  std::string create_log_out_message();
  std::string create_heartbeat_message(FIX8::Message* message);
  std::string create_order_message(
      const trading::NewSingleOrderData& order_data);
  std::string create_cancel_order_message(
      const trading::OrderCancelRequest& cancel_request);
  std::string create_cancel_and_reorder_message(
      const trading::OrderCancelRequestAndNewOrderSingle& cancel_and_re_order);
  std::string create_order_all_cancel(
      const trading::OrderMassCancelRequest& all_order_cancel);
  trading::ExecutionReport* create_excution_report_message(
      FIX8::NewOroFix44OE::ExecutionReport* msg);
  trading::OrderCancelReject* create_order_cancel_reject_message(
      FIX8::NewOroFix44OE::OrderCancelReject* msg);
  trading::OrderMassCancelReport* create_order_mass_cancel_report_message(
      FIX8::NewOroFix44OE::OrderMassCancelReport* msg);
  trading::OrderReject create_reject_message(FIX8::NewOroFix44OE::Reject* msg);
  FIX8::Message* decode(const std::string& message);

private:
  int64_t sequence_{1};
  const std::string sender_comp_id_;
  const std::string target_comp_id_;
  common::Logger* logger_;
  trading::ResponseManager* response_manager_;
  int qty_precision;
  int price_precision;
};
}