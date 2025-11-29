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

#ifndef ORDER_GATEWAY_TPP
#define ORDER_GATEWAY_TPP

#include "order_gateway.h"

namespace trading {
template <typename Strategy, typename OeApp>
class TradeEngine;

template <typename Strategy, typename OeApp>
requires core::OrderEntryAppLike<OeApp>
OrderGateway<Strategy, OeApp>::OrderGateway(common::Logger* logger,
    ResponseManager* response_manager)
    : logger_(logger->make_producer()),
      app_(std::make_unique<OeApp>("BMDWATCH", "SPOT", logger,
          response_manager)) {
  using WireMessage = typename OeApp::WireMessage;
  auto register_handler = [this](const std::string& type, auto&& fn) {
    if constexpr (std::is_pointer_v<WireMessage>) {
      app_->register_callback(type,
          [handler = std::forward<decltype(fn)>(fn)](
              WireMessage msg) { handler(msg); });
    } else {
      app_->register_callback(type,
          [handler = std::forward<decltype(fn)>(fn)](
              const WireMessage& msg) { handler(msg); });
    }
  };

  register_handler("A", [this](auto&& msg) { on_login(msg); });
  register_handler("1", [this](auto&& msg) { on_heartbeat(msg); });
  register_handler("5", [this](auto&& msg) { on_logout(msg); });

  register_handler("8", [this](auto&& msg) {
    if constexpr (std::is_pointer_v<WireExecutionReport>) {
      on_execution_report(reinterpret_cast<WireExecutionReport>(msg));
    } else {
      on_execution_report(std::get<WireExecutionReport>(msg));
    }
  });
  register_handler("9", [this](auto&& msg) {
    if constexpr (std::is_pointer_v<WireCancelReject>) {
      on_order_cancel_reject(reinterpret_cast<WireCancelReject>(msg));
    } else {
      on_order_cancel_reject(std::get<WireCancelReject>(msg));
    }
  });
  register_handler("r", [this](auto&& msg) {
    if constexpr (std::is_pointer_v<WireMassCancelReport>) {
      on_order_mass_cancel_report(reinterpret_cast<WireMassCancelReport>(msg));
    } else {
      on_order_mass_cancel_report(std::get<WireMassCancelReport>(msg));
    }
  });
  register_handler("3", [this](auto&& msg) {
    if constexpr (std::is_pointer_v<WireReject>) {
      on_rejected(reinterpret_cast<WireReject>(msg));
    } else {
      on_rejected(std::get<WireReject>(msg));
    }
  });

  if (!app_->start()) {
    logger_.info("Order Entry Start");
  }

  logger_.info("[Constructor] OrderGateway Created");
}

template <typename Strategy, typename OeApp>
requires core::OrderEntryAppLike<OeApp>
OrderGateway<Strategy, OeApp>::~OrderGateway() {
  logger_.info("[Destructor] OrderGateway Destroy");
}

template <typename Strategy, typename OeApp>
requires core::OrderEntryAppLike<OeApp>
void OrderGateway<Strategy, OeApp>::stop() const {
  app_->stop();
}

template <typename Strategy, typename OeApp>
requires core::OrderEntryAppLike<OeApp>
void OrderGateway<Strategy, OeApp>::init_trade_engine(
    TradeEngine<Strategy, OeApp>* trade_engine) {
  trade_engine_ = trade_engine;
}

template <typename Strategy, typename OeApp>
requires core::OrderEntryAppLike<OeApp>
void OrderGateway<Strategy, OeApp>::on_login(WireMessage /*msg*/) {
  logger_.info("[OrderGateway][Message] login successful");
}

template <typename Strategy, typename OeApp>
requires core::OrderEntryAppLike<OeApp>
void OrderGateway<Strategy, OeApp>::on_execution_report(
    WireExecutionReport msg) {
  ResponseCommon res;
  res.res_type = ResponseType::kExecutionReport;
  res.execution_report = app_->create_execution_report_message(msg);

  if (UNLIKELY(!trade_engine_->enqueue_response(res))) {
    logger_.error("[OrderGateway][Message] failed to send execution_report");
  }
}

template <typename Strategy, typename OeApp>
requires core::OrderEntryAppLike<OeApp>
void OrderGateway<Strategy, OeApp>::on_order_cancel_reject(
    WireCancelReject msg) {
  ResponseCommon res;
  res.res_type = ResponseType::kOrderCancelReject;
  res.order_cancel_reject = app_->create_order_cancel_reject_message(msg);

  if (UNLIKELY(!trade_engine_->enqueue_response(res))) {
    logger_.error("[OrderGateway][Message] failed to send order_cancel_reject");
  }
}

template <typename Strategy, typename OeApp>
requires core::OrderEntryAppLike<OeApp>
void OrderGateway<Strategy, OeApp>::on_order_mass_cancel_report(
    WireMassCancelReport msg) {
  ResponseCommon res;
  res.res_type = ResponseType::kOrderMassCancelReport;
  res.order_mass_cancel_report =
      app_->create_order_mass_cancel_report_message(msg);

  if (UNLIKELY(!trade_engine_->enqueue_response(res))) {
    logger_.error("[OrderGateway][Message] failed to send order_mass_cancel");
  }
}

template <typename Strategy, typename OeApp>
requires core::OrderEntryAppLike<OeApp>
void OrderGateway<Strategy, OeApp>::on_rejected(WireReject msg) {
  const OrderReject reject = app_->create_reject_message(msg);
  logger_.error(reject.toString());
  if (reject.session_reject_reason == "A") {
    app_->stop();
  }
}

template <typename Strategy, typename OeApp>
requires core::OrderEntryAppLike<OeApp>
void OrderGateway<Strategy, OeApp>::on_order_mass_status_response(
    WireMessage /*msg*/) {
  logger_.info("on_order_mass_status_response");
}

template <typename Strategy, typename OeApp>
requires core::OrderEntryAppLike<OeApp>
void OrderGateway<Strategy, OeApp>::on_logout(WireMessage /*msg*/) {
  auto message = app_->create_log_out_message();

  if (UNLIKELY(!app_->send(message))) {
    logger_.error("[OrderGateway][Message] failed to send logout");
  }
}

template <typename Strategy, typename OeApp>
requires core::OrderEntryAppLike<OeApp>
void OrderGateway<Strategy, OeApp>::on_heartbeat(WireMessage msg) {
  auto message = app_->create_heartbeat_message(msg);

  if (!message.empty() && UNLIKELY(!app_->send(message))) {
    logger_.error("[OrderGateway][Message] failed to send heartbeat");
  }
}

template <typename Strategy, typename OeApp>
requires core::OrderEntryAppLike<OeApp>
void OrderGateway<Strategy, OeApp>::order_request(
    const RequestCommon& request) {
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

template <typename Strategy, typename OeApp>
requires core::OrderEntryAppLike<OeApp>
void OrderGateway<Strategy, OeApp>::new_single_order_data(
    const RequestCommon& request) {
  const NewSingleOrderData order_data{.cl_order_id = request.cl_order_id,
      .symbol = request.symbol,
      .side = to_common_side(request.side),
      .order_qty = request.order_qty,
      .ord_type = request.ord_type,
      .price = request.price,
      .time_in_force = request.time_in_force,
      .self_trade_prevention_mode = request.self_trade_prevention_mode};

  const std::string msg = app_->create_order_message(order_data);
  logger_.info(std::format("[Message]Send order message:{}", msg));

  if (UNLIKELY(!app_->send(msg))) {
    logger_.error(
        std::format("[Message] failed to send new_single_order_data [msg:{}]",
            msg));
  }
}

template <typename Strategy, typename OeApp>
requires core::OrderEntryAppLike<OeApp>
void OrderGateway<Strategy, OeApp>::order_cancel_request(
    const RequestCommon& request) {
  const OrderCancelRequest cancel_request{.cl_order_id = request.cl_order_id,
      .orig_cl_order_id = request.orig_cl_order_id,
      .symbol = request.symbol};

  const std::string msg = app_->create_cancel_order_message(cancel_request);
  logger_.debug(std::format("[Message]Send cancel order message:{}", msg));

  if (UNLIKELY(!app_->send(msg))) {
    logger_.error("[Message] failed to send order_cancel_request");
  }
}

template <typename Strategy, typename OeApp>
requires core::OrderEntryAppLike<OeApp>
void OrderGateway<Strategy, OeApp>::order_cancel_request_and_new_order_single(
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

  const std::string msg =
      app_->create_cancel_and_reorder_message(cancel_and_reorder);
  logger_.debug(
      std::format("[Message]Send cancel and reorder message:{}", msg));

  if (UNLIKELY(!app_->send(msg))) {
    logger_.error("[Message] failed to create_cancel_and_new_order");
  }
}

template <typename Strategy, typename OeApp>
requires core::OrderEntryAppLike<OeApp>
void OrderGateway<Strategy, OeApp>::order_mass_cancel_request(
    const RequestCommon& request) {
  const OrderMassCancelRequest all_cancel_request{
      .cl_order_id = request.cl_order_id,
      .symbol = request.symbol};

  const std::string msg = app_->create_order_all_cancel(all_cancel_request);
  logger_.debug(std::format("[Message]Send cancel all orders message:{}", msg));

  if (UNLIKELY(!app_->send(msg))) {
    logger_.error("[Message] failed to send order_mass_cancel_request");
  }
}

}  // namespace trading

#endif  // ORDER_GATEWAY_TPP
