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
#include "fix_app.h"
#include "fix_oe_core.h"

namespace FIX8 {
class Message;
}

namespace core {

class FixOrderEntryApp : public FixApp<FixOrderEntryApp, 3> {
public:
  FixOrderEntryApp(const Authorization& authorization,
                   const std::string& sender_comp_id,
                   const std::string& target_comp_id, common::Logger* logger,
                   common::MemoryPool<OrderData>* order_data_pool):
    FixApp(authorization.oe_address,
           authorization.port,
           sender_comp_id,
           target_comp_id,
           logger,
           authorization)
    , order_data_pool_(order_data_pool) {
    fix_oe_core_ = std::make_unique<FixOeCore>(sender_comp_id, target_comp_id,
                                               logger);
  }

  std::string create_log_on_message(const std::string& sig_b64,
                                    const std::string& timestamp);
  std::string create_log_out_message();
  std::string create_heartbeat_message(FIX8::Message* message);
  std::string create_order_message(
      const trading::NewSingleOrderData& order_data);
  trading::ExecutionReport create_execution_report_message(
      FIX8::NewOroFix44OE::ExecutionReport* msg);
  FIX8::Message* decode(const std::string& message);

private:
  common::MemoryPool<OrderData>* order_data_pool_;
  std::unique_ptr<FixOeCore> fix_oe_core_;
};
}