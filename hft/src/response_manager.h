//
// Created by neworo2 on 25. 8. 9.
//

#pragma once
#include "memory_pool.hpp"
#include "order_entry.h"

namespace common {
class Logger;
}

namespace trading {
class ResponseManager {
 public:
  ResponseManager(
      common::Logger* logger,
      common::MemoryPool<ExecutionReport>* execution_report_pool,
      common::MemoryPool<OrderCancelReject>* order_cancel_reject_pool,
      common::MemoryPool<OrderMassCancelReport>* order_mass_cancel_report_pool);
  ~ResponseManager();

  ExecutionReport* execution_report_allocate();
  OrderCancelReject* order_cancel_reject_allocate();
  OrderMassCancelReport* order_mass_cancel_report_allocate();

  bool execution_report_deallocate(ExecutionReport* execution_report);
  bool order_cancel_reject_deallocate(OrderCancelReject* order_cancel_reject);
  bool order_mass_cancel_report_deallocate(
      OrderMassCancelReport* order_mass_cancel_report);

 private:
  common::Logger* logger_;
  common::MemoryPool<ExecutionReport>* execution_report_pool_;
  common::MemoryPool<OrderCancelReject>* order_cancel_reject_pool_;
  common::MemoryPool<OrderMassCancelReport>* order_mass_cancel_report_pool_;
};
}  // namespace trading