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

#ifndef SCHEMA_ORDER_H
#define SCHEMA_ORDER_H

#include <glaze/glaze.hpp>
#include "api_response.h"

namespace schema {
// clang-format off
struct OrderFill {
  double price{};
  double quantity{};
  double commission{};
  std::string commission_asset;
  std::int64_t trade_id{};
};

struct CancelOrderResult {
  std::string symbol;
  std::string original_client_order_id;   // origClientOrderId
  std::int64_t order_id{};                // orderId
  std::int64_t order_list_id{};           // orderListId
  std::string client_order_id;            // clientOrderId
  std::int64_t transact_time{};           // transactTime
  double price{};                         // price
  double original_quantity{};             // origQty
  double executed_quantity{};             // executedQty
  double original_quote_order_quantity{}; // origQuoteOrderQty
  double cumulative_quote_quantity{};     // cummulativeQuoteQty
  std::string status;                     // status
  std::string time_in_force;              // timeInForce
  std::string order_type;                 // type
  std::string side;                       // side
  double stop_price{};                    // stopPrice
  std::int64_t trailing_delta{};          // trailingDelta
  std::int64_t trailing_time{};           // trailingTime
  double iceberg_quantity{};              // icebergQty
  std::int64_t strategy_id{};             // strategyId
  std::int64_t strategy_type{};           // strategyType
  std::string self_trade_prevention_mode; // selfTradePreventionMode
};

struct PlaceOrderResult {
  std::string symbol;
  std::int64_t order_id{};
  std::int64_t order_list_id{};
  std::string client_order_id;
  std::int64_t transact_time{};

  double price{};
  double original_quantity{};
  double executed_quantity{};
  double original_quote_order_quantity{};
  double cumulative_quote_quantity{};
  std::string status;
  std::string time_in_force;
  std::string order_type;
  std::string side;
  std::int64_t working_time{};
  std::string self_trade_prevention_mode;

  std::vector<OrderFill> fills;
};

struct CancelAndReorderResult {
  std::string cancel_result;
  std::string new_order_result;
  CancelOrderResult cancel_response;
  PlaceOrderResult new_order_response;
};
struct ShortError {
  int code{};
  std::string msg;

  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = ShortError;
    static constexpr auto value = glz::object(  // NOLINT(readability-identifier-naming)
      "code", &T::code,
      "msg", &T::msg
    );
  };
};
struct CancelSuccess {
  std::string symbol;
  std::string orig_client_order_id;
  std::int64_t order_id{};
  std::int64_t order_list_id{};
  std::string client_order_id;
  std::int64_t transact_time{};
  std::string price;
  std::string orig_qty;
  std::string executed_qty;
  std::string orig_quote_order_qty;
  std::string cummulative_quote_qty;
  std::string status;
  std::string time_in_force;
  std::string type;
  std::string side;
  std::string self_trade_prevention_mode;

  // clang-format off
  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = CancelSuccess;
    static constexpr auto value = glz::object(  // NOLINT(readability-identifier-naming)
      "symbol", &T::symbol,
      "origClientOrderId", &T::orig_client_order_id,
      "orderId", &T::order_id,
      "orderListId", &T::order_list_id,
      "clientOrderId", &T::client_order_id,
      "transactTime", &T::transact_time,
      "price", &T::price,
      "origQty", &T::orig_qty,
      "executedQty", &T::executed_qty,
      "origQuoteOrderQty", &T::orig_quote_order_qty,
      "cummulativeQuoteQty", &T::cummulative_quote_qty,
      "status", &T::status,
      "timeInForce", &T::time_in_force,
      "type", &T::type,
      "side", &T::side,
      "selfTradePreventionMode", &T::self_trade_prevention_mode
    );
  };
  // clang-format on
};
using CancelResponseVariant =
    std::variant<std::monostate, ShortError, CancelSuccess>;
using NewOrderResponseVariant = std::variant<std::monostate, ShortError>;
struct CancelAndReorderErrorResponse {
  std::string cancel_result;    // "FAILURE" / "SUCCESS"
  std::string new_order_result;  // "NOT_ATTEMPTED" / "FAILURE" / "SUCCESS" ...

  CancelResponseVariant cancel_response;
  NewOrderResponseVariant new_order_response;

  // clang-format off
  // NOLINTNEXTLINE(readability-identifier-naming)
  struct glaze {
    using T = CancelAndReorderErrorResponse;
    static constexpr auto value = glz::object(  // NOLINT(readability-identifier-naming)
      "cancelResult", &T::cancel_result,
      "newOrderResult", &T::new_order_result,
      "cancelResponse", &T::cancel_response,
      "newOrderResponse", &T::new_order_response
    );
  };
  // clang-format on
};

struct CancelAllOrdersEntryOrder {
  std::string symbol;
  std::int64_t order_id{};
  std::string client_order_id;
};

struct CancelAllOrdersEntryOrderReport {
  std::string symbol;
  std::string original_client_order_id;
  std::int64_t order_id{};
  std::int64_t order_list_id{};
  std::string client_order_id;
  std::int64_t transact_time{};
  double price{};
  double original_quantity{};
  double executed_quantity{};
  double original_quote_order_quantity{};
  double cumulative_quote_quantity{};
  std::string status;
  std::string time_in_force;
  std::string order_type;
  std::string side;
  double stop_price{};
  std::string self_trade_prevention_mode;
};

struct CancelAllOrdersEntry {
  std::string symbol;
  std::string original_client_order_id;
  std::int64_t order_id{};
  std::int64_t order_list_id{};
  std::string client_order_id;
  std::int64_t transact_time{};
  double price{};
  double original_quantity{};
  double executed_quantity{};
  double original_quote_order_quantity{};
  double cumulative_quote_quantity{};
  std::string status;
  std::string time_in_force;
  std::string order_type;
  std::string side;
  double stop_price{};
  std::int64_t trailing_delta{};
  std::int64_t trailing_time{};
  double iceberg_quantity{};
  std::int64_t strategy_id{};
  std::int64_t strategy_type{};
  std::string self_trade_prevention_mode;

  std::string contingency_type;
  std::string list_status_type;
  std::string list_order_status;
  std::string list_client_order_id;
  std::int64_t transaction_time{};
  std::vector<CancelAllOrdersEntryOrder> orders;
  std::vector<CancelAllOrdersEntryOrderReport> order_reports;
};

template <typename ResultT, typename ErrorDataT = std::string>
struct WsApiResponse {
  std::string id;
  std::int32_t status{};
  std::optional<ResultT> result;
  std::optional<std::vector<RateLimit>> rate_limits;
  std::optional<ErrorResponse<ErrorDataT>> error;
};

using CancelOrderResponse = WsApiResponse<CancelOrderResult, CancelOrderResult>;
using CancelAndReorderResponse =
    WsApiResponse<CancelAndReorderResult, CancelAndReorderErrorResponse>;
using PlaceOrderResponse = WsApiResponse<PlaceOrderResult, PlaceOrderResult>;
using CancelAllOrdersResponse =
    WsApiResponse<std::vector<CancelAllOrdersEntry>, CancelAllOrdersEntry>;

}  // namespace schema

namespace glz {
template <>
struct meta<::schema::OrderFill> {
  using T = ::schema::OrderFill;
  static constexpr auto value = glz::object("price", glz::quoted_num<&T::price>,  // NOLINT(readability-identifier-naming)
      "qty", glz::quoted_num<&T::quantity>, "commission",
      glz::quoted_num<&T::commission>, "commissionAsset", &T::commission_asset,
      "tradeId", &T::trade_id);
};

template <>
struct meta<::schema::CancelOrderResult> {
  using T = ::schema::CancelOrderResult;
  static constexpr auto value = glz::object("symbol", &T::symbol,  // NOLINT(readability-identifier-naming)
      "origClientOrderId", &T::original_client_order_id, "orderId",
      &T::order_id, "orderListId", &T::order_list_id, "clientOrderId",
      &T::client_order_id, "transactTime", &T::transact_time, "price",
      glz::quoted_num<&T::price>, "origQty",
      glz::quoted_num<&T::original_quantity>, "executedQty",
      glz::quoted_num<&T::executed_quantity>, "origQuoteOrderQty",
      glz::quoted_num<&T::original_quote_order_quantity>, "cummulativeQuoteQty",
      glz::quoted_num<&T::cumulative_quote_quantity>, "status", &T::status,
      "timeInForce", &T::time_in_force, "type", &T::order_type, "side",
      &T::side, "stopPrice", glz::quoted_num<&T::stop_price>, "trailingDelta",
      &T::trailing_delta, "trailingTime", &T::trailing_time, "icebergQty",
      glz::quoted_num<&T::iceberg_quantity>, "strategyId", &T::strategy_id,
      "strategyType", &T::strategy_type, "selfTradePreventionMode",
      &T::self_trade_prevention_mode);
};

// PlaceOrderResult (ACK/RESULT/FULL 공통)
template <>
struct meta<::schema::PlaceOrderResult> {
  using T = ::schema::PlaceOrderResult;
  static constexpr auto value = glz::object("symbol", &T::symbol, "orderId",  // NOLINT(readability-identifier-naming)
      &T::order_id, "orderListId", &T::order_list_id, "clientOrderId",
      &T::client_order_id, "transactTime", &T::transact_time,

      "price", glz::quoted_num<&T::price>, "origQty",
      glz::quoted_num<&T::original_quantity>, "executedQty",
      glz::quoted_num<&T::executed_quantity>, "origQuoteOrderQty",
      glz::quoted_num<&T::original_quote_order_quantity>, "cummulativeQuoteQty",
      glz::quoted_num<&T::cumulative_quote_quantity>, "status", &T::status,
      "timeInForce", &T::time_in_force, "type", &T::order_type, "side",
      &T::side, "workingTime", &T::working_time, "selfTradePreventionMode",
      &T::self_trade_prevention_mode, "fills", &T::fills);
};

template <>
struct meta<::schema::CancelAndReorderResult> {
  using T = ::schema::CancelAndReorderResult;
  static constexpr auto value = glz::object("cancelResult", &T::cancel_result,  // NOLINT(readability-identifier-naming)
      "newOrderResult", &T::new_order_result, "cancelResponse",
      &T::cancel_response, "newOrderResponse", &T::new_order_response);
};

template <>
struct meta<::schema::CancelAllOrdersEntryOrder> {
  using T = ::schema::CancelAllOrdersEntryOrder;
  static constexpr auto value = glz::object("symbol", &T::symbol, "orderId",  // NOLINT(readability-identifier-naming)
      &T::order_id, "clientOrderId", &T::client_order_id);
};

template <>
struct meta<::schema::CancelAllOrdersEntryOrderReport> {
  using T = ::schema::CancelAllOrdersEntryOrderReport;
  static constexpr auto value = glz::object("symbol", &T::symbol,  // NOLINT(readability-identifier-naming)
      "origClientOrderId", &T::original_client_order_id, "orderId",
      &T::order_id, "orderListId", &T::order_list_id, "clientOrderId",
      &T::client_order_id, "transactTime", &T::transact_time, "price",
      glz::quoted_num<&T::price>, "origQty",
      glz::quoted_num<&T::original_quantity>, "executedQty",
      glz::quoted_num<&T::executed_quantity>, "origQuoteOrderQty",
      glz::quoted_num<&T::original_quote_order_quantity>, "cummulativeQuoteQty",
      glz::quoted_num<&T::cumulative_quote_quantity>, "status", &T::status,
      "timeInForce", &T::time_in_force, "type", &T::order_type, "side",
      &T::side, "stopPrice", glz::quoted_num<&T::stop_price>,
      "selfTradePreventionMode", &T::self_trade_prevention_mode);
};

template <>
struct meta<::schema::CancelAllOrdersEntry> {
  using T = ::schema::CancelAllOrdersEntry;
  static constexpr auto value = glz::object("symbol", &T::symbol,  // NOLINT(readability-identifier-naming)
      "origClientOrderId", &T::original_client_order_id, "orderId",
      &T::order_id, "orderListId", &T::order_list_id, "clientOrderId",
      &T::client_order_id, "transactTime", &T::transact_time, "price",
      glz::quoted_num<&T::price>, "origQty",
      glz::quoted_num<&T::original_quantity>, "executedQty",
      glz::quoted_num<&T::executed_quantity>, "origQuoteOrderQty",
      glz::quoted_num<&T::original_quote_order_quantity>, "cummulativeQuoteQty",
      glz::quoted_num<&T::cumulative_quote_quantity>, "status", &T::status,
      "timeInForce", &T::time_in_force, "type", &T::order_type, "side",
      &T::side, "stopPrice", glz::quoted_num<&T::stop_price>, "trailingDelta",
      &T::trailing_delta, "trailingTime", &T::trailing_time, "icebergQty",
      glz::quoted_num<&T::iceberg_quantity>, "strategyId", &T::strategy_id,
      "strategyType", &T::strategy_type, "selfTradePreventionMode",
      &T::self_trade_prevention_mode,

      "contingencyType", &T::contingency_type, "listStatusType",
      &T::list_status_type, "listOrderStatus", &T::list_order_status,
      "listClientOrderId", &T::list_client_order_id, "transactionTime",
      &T::transaction_time, "orders", &T::orders, "orderReports",
      &T::order_reports);
};

template <typename ResultT, typename ErrorDataT>
struct meta<::schema::WsApiResponse<ResultT, ErrorDataT>> {
  using T = ::schema::WsApiResponse<ResultT, ErrorDataT>;
  static constexpr auto value = glz::object("id", &T::id, "status", &T::status,  // NOLINT(readability-identifier-naming)
      "result", &T::result, "rateLimits", &T::rate_limits, "error", &T::error);
};
}  // namespace glz
// clang-format on
#endif  //SCHEMA_ORDER_H
