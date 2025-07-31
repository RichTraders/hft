//
// Created by neworo2 on 25. 7. 29.
//

#pragma once
#include "fix_app.h"
#include "fix_oe_core.h"

namespace FIX8 {
class Message;
}

namespace core {

class FixOrderEntryApp : public FixApp<FixOrderEntryApp> {
public:
  FixOrderEntryApp(const std::string& address, int port,
                   const std::string& sender_comp_id,
                   const std::string& target_comp_id, common::Logger* logger,
                   common::MemoryPool<OrderData>* order_data_pool):
    FixApp(address,
           port,
           sender_comp_id,
           target_comp_id,
           logger)
    , order_data_pool_(order_data_pool) {
    fix_oe_core_ = std::make_unique<FixOeCore>(sender_comp_id, target_comp_id,
                                               logger, order_data_pool);
  }

  std::string create_log_on_message(const std::string& sig_b64,
                                    const std::string& timestamp);
  std::string create_log_out_message();
  std::string create_heartbeat_message(FIX8::Message* message);
  std::string create_order_message(const trading::NewSingleOrderData& order_data);
  trading::ExecutionReport create_execution_report_message(FIX8::NewOroFix44OE::ExecutionReport* msg);
  FIX8::Message* decode(const std::string& message);
private:
  common::MemoryPool<OrderData>* order_data_pool_;
  std::unique_ptr<FixOeCore> fix_oe_core_;
};
}