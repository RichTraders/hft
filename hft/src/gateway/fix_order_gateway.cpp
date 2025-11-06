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

#include "fix_gateway.h"

#include "response_manager.h"
#include "trade_engine.h"

namespace trading {

FixGateway::FixGateway(const std::string& sender_comp_id,
                       const std::string& target_comp_id, common::Logger* logger,
                       ResponseManager* response_manager, TradeEngine* trade_engine)
    : logger_(logger->make_producer()),
      trade_engine_(trade_engine),
      app_(std::make_unique<core::FixOrderEntryApp>(sender_comp_id, target_comp_id,
                                                     logger, response_manager)) {
  // Register FIX message callbacks
  app_->register_callback(
      "A", [this](auto&& msg) { on_login(std::forward<decltype(msg)>(msg)); });
  app_->register_callback(
      "1", [this](auto&& msg) { on_heartbeat(std::forward<decltype(msg)>(msg)); });
  app_->register_callback("8", [this](FIX8::Message* msg) {
    on_execution_report(
        reinterpret_cast<FIX8::NewOroFix44OE::ExecutionReport*>(msg));
  });
  app_->register_callback("9", [this](FIX8::Message* msg) {
    on_order_cancel_reject(
        reinterpret_cast<FIX8::NewOroFix44OE::OrderCancelReject*>(msg));
  });
  app_->register_callback("r", [this](FIX8::Message* msg) {
    on_order_mass_cancel_report(
        reinterpret_cast<FIX8::NewOroFix44OE::OrderMassCancelReport*>(msg));
  });
  app_->register_callback("3", [this](FIX8::Message* msg) {
    on_rejected(reinterpret_cast<FIX8::NewOroFix44OE::Reject*>(msg));
  });
  app_->register_callback(
      "5", [this](auto&& msg) { on_logout(std::forward<decltype(msg)>(msg)); });

  if (!app_->start()) {
    logger_.info("Fix Order Entry Started");
  }

  logger_.info("[Constructor] FixGateway Constructor");
}

FixGateway::~FixGateway() {
  logger_.info("[Destructor] FixGateway Destroy");
}

void FixGateway::stop() {
  app_->stop();
}

void FixGateway::send_order(const RequestCommon& request) {
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
      logger_.info("[Message] invalid request type");
      break;
  }
}

void FixGateway::on_login(FIX8::Message*) {
  logger_.info("[Message] login successful");
}

void FixGateway::on_execution_report(FIX8::NewOroFix44OE::ExecutionReport* msg) {
  ResponseCommon res;
  res.res_type = ResponseType::kExecutionReport;
  res.execution_report = app_->create_execution_report_message(msg);

  if (UNLIKELY(!trade_engine_->enqueue_response(res))) {
    logger_.error("[Report] failed to send execution_report");
  }
}

void FixGateway::on_order_cancel_reject(
    FIX8::NewOroFix44OE::OrderCancelReject* msg) {
  ResponseCommon res;
  res.res_type = ResponseType::kOrderCancelReject;
  res.order_cancel_reject = app_->create_order_cancel_reject_message(msg);

  if (UNLIKELY(!trade_engine_->enqueue_response(res))) {
    logger_.error("[Reject] failed to send order_cancel_reject");
  }
}

void FixGateway::on_order_mass_cancel_report(
    FIX8::NewOroFix44OE::OrderMassCancelReport* msg) {
  ResponseCommon res;
  res.res_type = ResponseType::kOrderMassCancelReport;
  res.order_mass_cancel_report = app_->create_order_mass_cancel_report_message(msg);

  if (UNLIKELY(!trade_engine_->enqueue_response(res))) {
    logger_.error("[Report] failed to send order_mass_cancel");
  }
}

void FixGateway::on_rejected(FIX8::NewOroFix44OE::Reject* msg) {
  const OrderReject reject = app_->create_reject_message(msg);
  logger_.error(reject.toString());
  if (reject.session_reject_reason == "A") {
    app_->stop();
  }
}

void FixGateway::on_logout(FIX8::Message*) {
  auto message = app_->create_log_out_message();

  if (UNLIKELY(!app_->send(message))) {
    logger_.error("[Message] failed to send logout");
  }
}

void FixGateway::on_heartbeat(FIX8::Message* msg) {
  auto message = app_->create_heartbeat_message(msg);

  if (UNLIKELY(!app_->send(message))) {
    logger_.error("[Message] failed to send heartbeat");
  }
}

void FixGateway::new_single_order_data(const RequestCommon& request) {
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
  logger_.info(std::format("[Message] Send order message: {}", msg));

  if (UNLIKELY(!app_->send(msg))) {
    logger_.error(
        std::format("[Message] failed to send new_single_order_data [msg: {}]", msg));
  }
}

void FixGateway::order_cancel_request(const RequestCommon& request) {
  const OrderCancelRequest cancel_request{.cl_order_id = request.cl_order_id,
                                          .orig_cl_order_id = request.orig_cl_order_id,
                                          .symbol = request.symbol};

  const std::string msg = app_->create_cancel_order_message(cancel_request);
  logger_.debug(std::format("[Message] Send cancel order message: {}", msg));

  if (UNLIKELY(!app_->send(msg))) {
    logger_.error("[Message] failed to send order_cancel_request");
  }
}

void FixGateway::order_cancel_request_and_new_order_single(
    const RequestCommon& request) {
  const OrderCancelRequestAndNewOrderSingle cancel_and_reorder{
      .order_cancel_request_and_new_order_single_mode = 1,
      .cancel_new_order_id = request.cl_cancel_order_id,
      .cl_new_order_id = request.cl_order_id,
      .cl_origin_order_id = request.orig_cl_order_id,
      .symbol = request.symbol,
      .side = to_common_side(request.side),
      .order_qty = request.order_qty,
      .ord_type = request.ord_type,
      .price = request.price,
      .time_in_force = request.time_in_force,
      .self_trade_prevention_mode = request.self_trade_prevention_mode};

  const std::string msg = app_->create_cancel_and_reorder_message(cancel_and_reorder);
  logger_.debug(std::format("[Message] Send cancel and reorder message: {}", msg));

  if (UNLIKELY(!app_->send(msg))) {
    logger_.error("[Message] failed to create_cancel_and_new_order");
  }
}

void FixGateway::order_mass_cancel_request(const RequestCommon& request) {
  const OrderMassCancelRequest all_cancel_request{.cl_order_id = request.cl_order_id,
                                                   .symbol = request.symbol};

  const std::string msg = app_->create_order_all_cancel(all_cancel_request);
  logger_.debug(std::format("[Message] Send cancel all orders message: {}", msg));

  if (UNLIKELY(!app_->send(msg))) {
    logger_.error("[Message] failed to send order_mass_cancel_request");
  }
}

}  // namespace trading
