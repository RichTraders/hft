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

#include "order_gateway.h"
#include "trade_engine.h"

namespace trading {

OrderGateway::OrderGateway(const Authorization& authorization,
                           common::Logger* logger, TradeEngine* trade_engine)
    : logger_(logger),
      trade_engine_(trade_engine),
      app_(std::make_unique<core::FixOrderEntryApp>(authorization, "BMDWATCH",
                                                    "SPOT", logger_)) {

  constexpr int kRequestQueueSize = 64;
  order_queue_ = std::make_unique<common::SPSCQueue<trading::RequestCommon>>(
      kRequestQueueSize);
  write_thread_.start(&OrderGateway::write_loop, this);

  app_->register_callback(
      "A", [this](auto&& msg) { on_login(std::forward<decltype(msg)>(msg)); });
  app_->register_callback("1", [this](auto&& msg) {
    on_heartbeat(std::forward<decltype(msg)>(msg));
  });
  app_->register_callback("8", [&](FIX8::Message* msg) {
    on_execution_report(
        reinterpret_cast<FIX8::NewOroFix44OE::ExecutionReport*>(msg));
  });
  app_->register_callback("9", [&](FIX8::Message* msg) {
    on_order_cancel_reject(
        reinterpret_cast<FIX8::NewOroFix44OE::OrderCancelReject*>(msg));
  });
  app_->register_callback("r", [&](FIX8::Message* msg) {
    on_order_mass_cancel_report(
        reinterpret_cast<FIX8::NewOroFix44OE::OrderMassCancelReport*>(msg));
  });
  app_->register_callback(
      "5", [this](auto&& msg) { on_logout(std::forward<decltype(msg)>(msg)); });

  app_->start();
}

OrderGateway::~OrderGateway() {
  app_->stop();
}

void OrderGateway::on_login(FIX8::Message*) {
  logger_->info("login successful");
  logger_->info("sent order message");
}

void OrderGateway::on_execution_report(
    FIX8::NewOroFix44OE::ExecutionReport* msg) {
  ResponseCommon res;
  res.res_type = ResponseType::kExecutionReport;
  res.execution_report = app_->create_execution_report_message(msg);
  trade_engine_->enqueue_response(res);

  logger_->info("on_execution_report");
}

void OrderGateway::on_order_cancel_reject(
    FIX8::NewOroFix44OE::OrderCancelReject* msg) {
  ResponseCommon res;
  res.res_type = ResponseType::kOrderCancelReject;
  res.order_cancel_reject = app_->create_order_cancel_reject_message(msg);
  trade_engine_->enqueue_response(res);

  logger_->info("on_order_cancel_reject");
}

void OrderGateway::on_order_mass_cancel_report(
    FIX8::NewOroFix44OE::OrderMassCancelReport* msg) {
  ResponseCommon res;
  res.res_type = ResponseType::kOrderMassCancelReport;
  res.order_mass_cancel_report =
      app_->create_order_mass_cancel_report_message(msg);
  trade_engine_->enqueue_response(res);

  logger_->info("on_order_mass_cancel_report");
}

void OrderGateway::on_order_mass_status_response(FIX8::Message* msg) {
  (void)msg;
  logger_->info("on_order_mass_status_response");
}

void OrderGateway::on_logout(FIX8::Message*) {
  logger_->info("logout");
}

void OrderGateway::on_heartbeat(FIX8::Message* msg) {
  auto message = app_->create_heartbeat_message(msg);
  app_->send(message);
}

void OrderGateway::order_request(const RequestCommon& request) {
  order_queue_->enqueue(request);
}

void OrderGateway::write_loop() {
  while (thread_running_) {
    RequestCommon request;
    while (order_queue_->dequeue(request)) {
      switch (request.req_type) {
        case ReqeustType::kNewSingleOrderData:
          new_single_order_data(request);
          break;
        case ReqeustType::kOrderCancelRequest:
          order_cancel_request(request);
          break;
        case ReqeustType::kOrderCancelRequestAndNewOrderSingle:
          order_cancel_request_and_new_order_single(request);
          break;
        case ReqeustType::kOrderMassCancelRequest:
          order_mass_cancel_request(request);
          break;
        case ReqeustType::kInvalid:
        default:
          break;
      }
    }
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(kWriteThreadSleep));
}

void OrderGateway::new_single_order_data(const RequestCommon& request) {
  const NewSingleOrderData order_data{
      .cl_order_id = request.cl_order_id,
      .symbol = request.symbol,
      .side = to_common_side(request.side),
      .order_qty = request.order_qty,
      .ord_type = request.ord_type,
      .price = request.price,
      .time_in_force = request.time_in_force,
      .self_trade_prevention_mode = request.self_trade_prevention_mode};

  const std::string msg = app_->create_order_message(order_data);
  app_->send(msg);
}

void OrderGateway::order_cancel_request(const RequestCommon& request) {
  const OrderCancelRequest cancel_request{.cl_order_id = request.cl_order_id,
                                          .symbol = request.symbol};

  const std::string msg = app_->create_cancel_order_message(cancel_request);
  app_->send(msg);
}

void OrderGateway::order_cancel_request_and_new_order_single(
    const RequestCommon& request) {
  const OrderCancelRequestAndNewOrderSingle cancel_and_reorder{
      .order_cancel_request_and_new_order_single_mode = 1,
      .cancel_ord_id = 1,
      .cl_order_id = request.cl_order_id,
      .symbol = request.symbol,
      .side = to_common_side(request.side),
      .order_qty = request.order_qty,
      .ord_type = request.ord_type,
      .price = request.price,
      .time_in_force = request.time_in_force,
      .self_trade_prevention_mode = request.self_trade_prevention_mode};

  const std::string msg =
      app_->create_cancel_and_reorder_message(cancel_and_reorder);
  app_->send(msg);
}

void OrderGateway::order_mass_cancel_request(const RequestCommon& request) {
  const OrderMassCancelRequest all_cancel_request{
      .cl_order_id = request.cl_order_id, .symbol = request.symbol};

  const std::string msg = app_->create_order_all_cancel(all_cancel_request);
  app_->send(msg);
}

}  // namespace trading