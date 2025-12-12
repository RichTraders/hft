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

  OrderGateway(common::Logger* logger, ResponseManager* response_manager);
  ~OrderGateway();

  void init_trade_engine(TradeEngine<Strategy>* trade_engine);
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

  // Callback registration helpers
  template <typename Handler>
  void register_simple_callback(const std::string& type, Handler&& handler);

  template <typename TargetType, typename Handler>
  void register_typed_callback(const std::string& type, Handler&& handler);

  common::Logger::Producer logger_;
  TradeEngine<Strategy>* trade_engine_;

  std::unique_ptr<OeApp> app_;
};
}  // namespace trading

#endif