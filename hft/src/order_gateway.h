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
#ifndef ORDER_GATEWAY_H
#define ORDER_GATEWAY_H

#include "logger.h"
#include "order_entry.h"
#include "protocol_concepts.h"

#ifdef ENABLE_WEBSOCKET
#include "core/websocket/order_entry/ws_oe_app.h"
#else
#include "fix/fix_oe_app.h"
#endif

namespace trading {
template <typename Strategy, typename App>
class TradeEngine;
class ResponseManager;

template <typename Strategy, typename OeApp>
  requires core::OrderEntryAppLike<OeApp>
class OrderGateway {
 public:
  using AppType = OeApp;
  using WireMessage = typename OeApp::WireMessage;
  using WireExecutionReport = typename OeApp::WireExecutionReport;
  using WireCancelReject = typename OeApp::WireCancelReject;
  using WireMassCancelReport = typename OeApp::WireMassCancelReport;
  using WireReject = typename OeApp::WireReject;

  OrderGateway(common::Logger* logger, ResponseManager* response_manager);
  ~OrderGateway();

  void init_trade_engine(TradeEngine<Strategy, OeApp>* trade_engine);
  void stop() const;

  void on_login(WireMessage msg);
  void on_execution_report(WireExecutionReport msg);
  void on_order_cancel_reject(WireCancelReject msg);
  void on_order_mass_cancel_report(WireMassCancelReport msg);
  void on_rejected(WireReject msg);
  void on_order_mass_status_response(WireMessage msg);
  void on_logout(WireMessage msg);
  void on_heartbeat(WireMessage msg);
  void order_request(const RequestCommon& request);

 private:
  void new_single_order_data(const RequestCommon& request);
  void order_cancel_request(const RequestCommon& request);
  void order_cancel_request_and_new_order_single(const RequestCommon& request);
  void order_mass_cancel_request(const RequestCommon& request);

  common::Logger::Producer logger_;
  TradeEngine<Strategy, OeApp>* trade_engine_;

  std::unique_ptr<OeApp> app_;
};

#ifdef ENABLE_WEBSOCKET
template <typename Strategy>
using ProtocolOrderGateway = OrderGateway<Strategy, core::WsOrderEntryApp>;
#else
template <typename Strategy>
using ProtocolOrderGateway = OrderGateway<Strategy, core::FixOrderEntryApp>;
#endif
}  // namespace trading

#endif