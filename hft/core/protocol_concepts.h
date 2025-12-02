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

#ifndef PROTOCOL_CONCEPTS_H
#define PROTOCOL_CONCEPTS_H

#include "core/market_data.h"
#include "core/order_entry.h"

namespace trading {
class ResponseManager;
struct NewSingleOrderData;
struct OrderCancelRequest;
struct OrderCancelAndNewOrderSingle;
struct OrderMassCancelRequest;
}  // namespace trading
namespace common {
class Logger;
template <typename T>
class MemoryPool;
}  // namespace common

namespace core {

template <typename Core>
concept MarketDataCore =
    requires(Core core, typename Core::WireMessage msg, const std::string& sig,
        const std::string& timestamp, const std::string& request_id,
        const std::string& market_depth, const std::string& symbol) {
      typename Core::WireMessage;
      {
        core.create_log_on_message(sig, timestamp)
      } -> std::convertible_to<std::string>;
      { core.create_log_out_message() } -> std::convertible_to<std::string>;
      {
        core.create_heartbeat_message(msg)
      } -> std::convertible_to<std::string>;
      {
        core.create_market_data_subscription_message(request_id,
            market_depth,
            symbol,
            true)
      } -> std::convertible_to<std::string>;
      {
        core.create_trade_data_subscription_message(request_id,
            market_depth,
            symbol)
      } -> std::convertible_to<std::string>;
      {
        core.create_instrument_list_request_message(symbol)
      } -> std::convertible_to<std::string>;
      {
        core.create_market_data_message(msg)
      } -> std::same_as<MarketUpdateData>;
      {
        core.create_snapshot_data_message(msg)
      } -> std::same_as<MarketUpdateData>;
      {
        core.create_instrument_list_message(msg)
      } -> std::same_as<InstrumentInfo>;
      { core.create_reject_message(msg) } -> std::same_as<MarketDataReject>;
      {
        core.decode(std::declval<std::string>())
      } -> std::same_as<typename Core::WireMessage>;
    };

template <typename Core>
concept OrderEntryCore = requires(Core core,
    typename Core::WireMessage wire_msg,
    typename Core::WireExecutionReport exec_msg,
    typename Core::WireCancelReject cancel_msg,
    typename Core::WireMassCancelReport mass_msg,
    typename Core::WireReject reject_msg, const std::string& sig,
    const std::string& timestamp, const trading::NewSingleOrderData& new_order,
    const trading::OrderCancelRequest& cancel_request,
    const trading::OrderCancelAndNewOrderSingle& cancel_replace,
    const trading::OrderMassCancelRequest& mass_cancel) {
  typename Core::WireMessage;
  typename Core::WireExecutionReport;
  typename Core::WireCancelReject;
  typename Core::WireMassCancelReport;
  typename Core::WireReject;
  {
    core.create_log_on_message(sig, timestamp)
  } -> std::convertible_to<std::string>;
  { core.create_log_out_message() } -> std::convertible_to<std::string>;
  {
    core.create_heartbeat_message(wire_msg)
  } -> std::convertible_to<std::string>;
  { core.create_order_message(new_order) } -> std::convertible_to<std::string>;
  {
    core.create_cancel_order_message(cancel_request)
  } -> std::convertible_to<std::string>;
  {
    core.create_cancel_and_reorder_message(cancel_replace)
  } -> std::convertible_to<std::string>;
  {
    core.create_order_all_cancel(mass_cancel)
  } -> std::convertible_to<std::string>;
  {
    core.create_execution_report_message(exec_msg)
  } -> std::same_as<trading::ExecutionReport*>;
  {
    core.create_order_cancel_reject_message(cancel_msg)
  } -> std::same_as<trading::OrderCancelReject*>;
  {
    core.create_order_mass_cancel_report_message(mass_msg)
  } -> std::same_as<trading::OrderMassCancelReport*>;
  {
    core.create_reject_message(reject_msg)
  } -> std::same_as<trading::OrderReject>;
  {
    core.decode(std::declval<std::string>())
  } -> std::same_as<typename Core::WireMessage>;
};

template <typename T>
concept MarketDataAppLike =
    requires { typename T::WireMessage; } &&
    std::constructible_from<T, const std::string&, const std::string&,
        common::Logger*, common::MemoryPool<MarketData>*> &&
    requires(T app, const std::string& sig_b64, const std::string& timestamp,
        typename T::WireMessage msg, const std::string& request_id,
        const std::string& level, const std::string& symbol,
        const std::string& symbol_str) {
      {
        app.create_log_on_message(sig_b64, timestamp)
      } -> std::same_as<std::string>;
      { app.create_log_out_message() } -> std::same_as<std::string>;
      { app.create_heartbeat_message(msg) } -> std::same_as<std::string>;
      {
        app.create_market_data_subscription_message(request_id,
            level,
            symbol,
            true)
      } -> std::same_as<std::string>;
      {
        app.create_trade_data_subscription_message(request_id, level, symbol)
      } -> std::same_as<std::string>;
      { app.create_market_data_message(msg) } -> std::same_as<MarketUpdateData>;
      {
        app.create_snapshot_data_message(msg)
      } -> std::same_as<MarketUpdateData>;
      {
        app.request_instrument_list_message(symbol_str)
      } -> std::same_as<std::string>;
      {
        app.create_instrument_list_message(msg)
      } -> std::same_as<InstrumentInfo>;
      { app.create_reject_message(msg) } -> std::same_as<MarketDataReject>;
    };

template <typename T>
concept OrderEntryAppLike =
    requires {
      typename T::WireMessage;
      typename T::WireExecutionReport;
      typename T::WireCancelReject;
      typename T::WireMassCancelReport;
      typename T::WireReject;
    } &&
    std::constructible_from<T, const std::string&, const std::string&,
        common::Logger*, trading::ResponseManager*> &&
    requires(T app, const std::string& sig_b64, const std::string& timestamp,
        typename T::WireMessage wire_msg,
        const trading::NewSingleOrderData& new_order,
        const trading::OrderCancelRequest& cancel_req,
        const trading::OrderCancelAndNewOrderSingle& cancel_reorder,
        const trading::OrderMassCancelRequest& mass_cancel_req,
        typename T::WireExecutionReport exec_msg,
        typename T::WireCancelReject cancel_reject_msg,
        typename T::WireMassCancelReport mass_cancel_msg,
        typename T::WireReject reject_msg, const std::string& raw) {
      {
        app.create_log_on_message(sig_b64, timestamp)
      } -> std::same_as<std::string>;
      { app.create_log_out_message() } -> std::same_as<std::string>;

      { app.create_heartbeat_message(wire_msg) } -> std::same_as<std::string>;

      { app.create_order_message(new_order) } -> std::same_as<std::string>;
      {
        app.create_cancel_order_message(cancel_req)
      } -> std::same_as<std::string>;
      {
        app.create_cancel_and_reorder_message(cancel_reorder)
      } -> std::same_as<std::string>;
      {
        app.create_order_all_cancel(mass_cancel_req)
      } -> std::same_as<std::string>;

      {
        app.create_execution_report_message(exec_msg)
      } -> std::same_as<trading::ExecutionReport*>;
      {
        app.create_order_cancel_reject_message(cancel_reject_msg)
      } -> std::same_as<trading::OrderCancelReject*>;
      {
        app.create_order_mass_cancel_report_message(mass_cancel_msg)
      } -> std::same_as<trading::OrderMassCancelReport*>;
      {
        app.create_reject_message(reject_msg)
      } -> std::same_as<trading::OrderReject>;
      { app.post_new_order(new_order) } -> std::same_as<void>;
      { app.post_cancel_order(cancel_req) } -> std::same_as<void>;
      { app.post_cancel_and_reorder(cancel_reorder) } -> std::same_as<void>;
      { app.post_mass_cancel_order(mass_cancel_req) } -> std::same_as<void>;
    };

}  // namespace core

#endif