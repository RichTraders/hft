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
#ifndef ORDER_GATEWAY_HPP
#define ORDER_GATEWAY_HPP

#include "logger.h"
#include "message_adapter_policy.h"
#include "order_entry.h"
#include "protocol_impl.h"

namespace trading {
template <typename Strategy>
class TradeEngine;
class ResponseManager;

template <typename Strategy>
class OrderGateway {
 public:
  using OeApp = protocol_impl::OrderEntryApp;
  using AppType = OeApp;
  using MessagePolicy = typename MessagePolicySelector<OeApp>::type;
  using WireMessage = typename OeApp::WireMessage;
  using WireExecutionReport = typename OeApp::WireExecutionReport;
  using WireCancelReject = typename OeApp::WireCancelReject;
  using WireMassCancelReport = typename OeApp::WireMassCancelReport;
  using WireReject = typename OeApp::WireReject;

  OrderGateway(const common::Logger::Producer& logger,
      ResponseManager* response_manager)
      : logger_(logger),
        app_(std::make_unique<OeApp>("BMDWATCH", "SPOT", logger_,
            response_manager)) {

    register_simple_callback("A", [this](auto msg) { on_login(msg); });
    register_simple_callback("1", [this](auto msg) { on_heartbeat(msg); });
    register_simple_callback("5", [this](auto msg) { on_logout(msg); });

    register_typed_callback<WireExecutionReport>("8",
        [this](auto msg) { on_execution_report(msg); });

    register_typed_callback<WireCancelReject>("9",
        [this](auto msg) { on_order_cancel_reject(msg); });

    register_typed_callback<WireMassCancelReport>("r",
        [this](auto msg) { on_order_mass_cancel_report(msg); });

    register_typed_callback<WireReject>("3",
        [this](auto msg) { on_rejected(msg); });

    if (!app_->start()) {
      logger_.info("Order Entry Start");
    }

    logger_.info("[Constructor] OrderGateway Created");
  }

  ~OrderGateway() { std::cout << "[Destructor] OrderGateway Destroy\n"; }

  void init_trade_engine(TradeEngine<Strategy>* trade_engine) {
    trade_engine_ = trade_engine;
  }

  void stop() const { app_->stop(); }

  [[nodiscard]] bool is_ready() const noexcept {
    return app_->is_session_ready();
  }

  void on_login(const WireMessage& /*msg*/) {
    logger_.info("[OrderGateway][Message] login successful");
    app_->set_session_ready();
  }

  void on_execution_report(const WireExecutionReport& msg) {
    ResponseCommon res;
    res.res_type = ResponseType::kExecutionReport;
    res.execution_report = app_->create_execution_report_message(msg);

    if (UNLIKELY(!trade_engine_->enqueue_response(res))) {
      logger_.error("[OrderGateway][Message] failed to send execution_report");
    }
  }

  void on_order_cancel_reject(const WireCancelReject& msg) {
    ResponseCommon res;
    res.res_type = ResponseType::kOrderCancelReject;
    res.order_cancel_reject = app_->create_order_cancel_reject_message(msg);

    if (UNLIKELY(!trade_engine_->enqueue_response(res))) {
      logger_.error(
          "[OrderGateway][Message] failed to send order_cancel_reject");
    }
  }

  void on_order_mass_cancel_report(const WireMassCancelReport& msg) {
    ResponseCommon res;
    res.res_type = ResponseType::kOrderMassCancelReport;
    res.order_mass_cancel_report =
        app_->create_order_mass_cancel_report_message(msg);

    if (UNLIKELY(!trade_engine_->enqueue_response(res))) {
      logger_.error("[OrderGateway][Message] failed to send order_mass_cancel");
    }
  }

  void on_rejected(const WireReject& msg) {
    const OrderReject reject = app_->create_reject_message(msg);
    logger_.error(reject.toString());
    if (reject.session_reject_reason == "A") {
      app_->stop();
    }
  }

  void on_order_mass_status_response(const WireMessage& /*msg*/) {
    logger_.info("on_order_mass_status_response");
  }

  void on_logout(const WireMessage& /*msg*/) {
    auto message = app_->create_log_out_message();

    if (UNLIKELY(!app_->send(message))) {
      logger_.error("[OrderGateway][Message] failed to send logout");
    }
  }

  void on_heartbeat(WireMessage msg) {
    auto message = app_->create_heartbeat_message(std::move(msg));

    if (!message.empty() && UNLIKELY(!app_->send(message))) {
      logger_.error("[OrderGateway][Message] failed to send heartbeat");
    }
  }

  void order_request(const RequestCommon& request) {
    switch (request.req_type) {
      case ReqeustType::kNewSingleOrderData:
        new_single_order_data(request);
        break;
      case ReqeustType::kOrderCancelRequest:
        order_cancel_request(request);
        break;
#ifdef ENABLE_WEBSOCKET
      case ReqeustType::kOrderCancelRequestAndNewOrderSingle:
        order_cancel_request_and_new_order_single(request);
        break;
      case ReqeustType::kOrderModify:
        order_modify(request);
        break;
#endif
      case ReqeustType::kOrderMassCancelRequest:
        order_mass_cancel_request(request);
        break;
      case ReqeustType::kInvalid:
      default:
        logger_.info("[Message] invalid request type");
        break;
    }
  }

 private:
  void new_single_order_data(const RequestCommon& request) {
    const NewSingleOrderData order_data{.cl_order_id = request.cl_order_id,
        .symbol = request.symbol,
        .side = from_common_side(request.side),
        .order_qty = request.order_qty,
        .ord_type = request.ord_type,
        .price = request.price,
        .time_in_force = request.time_in_force,
        .self_trade_prevention_mode = request.self_trade_prevention_mode,
        .position_side = request.position_side};

    const std::string msg = app_->create_order_message(order_data);
    logger_.info("[Message]Send order message:{}", msg);

    if (UNLIKELY(!app_->send(msg))) {
      logger_.error("[Message] failed to send new_single_order_data [msg:{}]",
          msg);
    } else {
      app_->post_new_order(order_data);
    }
  }

  void order_cancel_request(const RequestCommon& request) {
    const OrderCancelRequest cancel_request{.cl_order_id = request.cl_order_id,
        .orig_cl_order_id = request.orig_cl_order_id,
        .symbol = request.symbol,
        .position_side = request.position_side};

    const std::string msg = app_->create_cancel_order_message(cancel_request);
    logger_.debug("[Message]Send cancel order message:{}", msg);

    if (UNLIKELY(!app_->send(msg))) {
      logger_.error("[Message] failed to send order_cancel_request");
    } else {
      app_->post_cancel_order(cancel_request);
    }
  }

#ifdef ENABLE_WEBSOCKET
  void order_cancel_request_and_new_order_single(const RequestCommon& request) {
    const OrderCancelAndNewOrderSingle cancel_and_reorder{
        .order_cancel_request_and_new_order_single_mode = 1,
        .cancel_new_order_id = request.cl_cancel_order_id,
        .cl_new_order_id = request.cl_order_id,
        .cl_origin_order_id = request.orig_cl_order_id,
        .symbol = request.symbol,
        .side = from_common_side(request.side),
        .order_qty = request.order_qty,
        .ord_type = request.ord_type,
        .price = request.price,
        .time_in_force = request.time_in_force,
        .self_trade_prevention_mode = request.self_trade_prevention_mode,
        .position_side = request.position_side};

    const std::string msg =
        app_->create_cancel_and_reorder_message(cancel_and_reorder);
    logger_.debug("[Message]Send cancel and reorder message:{}", msg);

    if (UNLIKELY(!app_->send(msg))) {
      logger_.error("[Message] failed to create_cancel_and_new_order");
    } else {
      app_->post_cancel_and_reorder(cancel_and_reorder);
    }
  }

  void order_modify(const RequestCommon& request) {
    const OrderModifyRequest modify_request{
        .orig_client_order_id = request.orig_cl_order_id,
        .symbol = request.symbol,
        .side = from_common_side(request.side),
        .price = request.price,
        .order_qty = request.order_qty,
        .position_side = request.position_side};

    const std::string msg = app_->create_modify_order_message(modify_request);
    logger_.debug("[Message]Send modify order message:{}", msg);

    if (UNLIKELY(!app_->send(msg))) {
      logger_.error("[Message] failed to send order_modify");
    } else {
      app_->post_modify_order(modify_request);
    }
  }
#endif

  void order_mass_cancel_request(const RequestCommon& request) {
    const OrderMassCancelRequest all_cancel_request{
        .cl_order_id = request.cl_order_id,
        .symbol = request.symbol};

    const std::string msg = app_->create_order_all_cancel(all_cancel_request);
    logger_.debug("[Message]Send cancel all orders message:{}", msg);

    if (UNLIKELY(!app_->send(msg))) {
      logger_.error("[Message] failed to send order_mass_cancel_request");
    } else {
      app_->post_mass_cancel_order(all_cancel_request);
    }
  }

  template <typename Handler>
  void register_simple_callback(const std::string& type, Handler&& handler) {
    app_->register_callback(type,
        [func = std::forward<Handler>(handler)](
            auto&& msg) { func(MessagePolicy::adapt(msg)); });
  }

  template <typename TargetType, typename Handler>
  void register_typed_callback(const std::string& type, Handler&& handler) {
    app_->register_callback(type,
        [func = std::forward<Handler>(handler)](
            auto&& msg) { func(MessagePolicy::extract<TargetType>(msg)); });
  }

  const common::Logger::Producer& logger_;
  TradeEngine<Strategy>* trade_engine_;

  std::unique_ptr<OeApp> app_;
};

}  // namespace trading

#endif  // ORDER_GATEWAY_HPP
