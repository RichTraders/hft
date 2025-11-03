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

#include "response_manager.h"
#include "logger.h"

namespace trading {
ResponseManager::ResponseManager(
    common::Logger* logger,
    common::MemoryPool<ExecutionReport>* execution_report_pool,
    common::MemoryPool<OrderCancelReject>* order_cancel_reject_pool,
    common::MemoryPool<OrderMassCancelReport>* order_mass_cancel_report_pool)
    : logger_(logger->make_producer()),
      execution_report_pool_(execution_report_pool),
      order_cancel_reject_pool_(order_cancel_reject_pool),
      order_mass_cancel_report_pool_(order_mass_cancel_report_pool) {
  logger_.info("[Constructor] Response manager initialized");
}

ResponseManager::~ResponseManager() {
  logger_.info("[Destructor] Response manager deinitialized");
}

ExecutionReport* ResponseManager::execution_report_allocate() {
  return execution_report_pool_->allocate();
}

OrderCancelReject* ResponseManager::order_cancel_reject_allocate() {
  return order_cancel_reject_pool_->allocate();
}

OrderMassCancelReport* ResponseManager::order_mass_cancel_report_allocate() {
  return order_mass_cancel_report_pool_->allocate();
}

bool ResponseManager::execution_report_deallocate(
    ExecutionReport* execution_report) {
  return execution_report_pool_->deallocate(execution_report);
}

bool ResponseManager::order_cancel_reject_deallocate(
    OrderCancelReject* order_cancel_reject) {
  return order_cancel_reject_pool_->deallocate(order_cancel_reject);
}

bool ResponseManager::order_mass_cancel_report_deallocate(
    OrderMassCancelReport* order_mass_cancel_report) {
  return order_mass_cancel_report_pool_->deallocate(order_mass_cancel_report);
}

}  // namespace trading
