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

#ifndef BINANCE_FUTURES_OE_DISPATCHER_TPP
#define BINANCE_FUTURES_OE_DISPATCHER_TPP

template <typename ExchangeTraits>
void BinanceFuturesOeDispatchRouter::process_message(
    const typename ExchangeTraits::WireMessage& message,
    const core::WsOeDispatchContext<ExchangeTraits>& context) {

  std::visit([&context, &message](auto&& arg) {
    using T = std::decay_t<decltype(arg)>;

    if constexpr (std::is_same_v<T, typename ExchangeTraits::ExecutionReportResponse>) {
      handle_execution_report<ExchangeTraits>(arg, context, message);
    }
    else if constexpr (std::is_same_v<T, typename ExchangeTraits::SessionLogonResponse>) {
      handle_session_logon<ExchangeTraits>(arg, context, message);
    }
    else if constexpr (std::is_same_v<T, typename ExchangeTraits::SessionUserSubscriptionResponse>) {
      handle_user_subscription<ExchangeTraits>(arg, context, message);
    }
    else if constexpr (std::is_same_v<T, typename ExchangeTraits::CancelAndReorderResponse>) {
      // Only handle if exchange supports cancel_and_reorder
      if constexpr (ExchangeTraits::supports_cancel_and_reorder()) {
        handle_cancel_and_reorder_response<ExchangeTraits>(arg, context, message);
      }
    }
    else if constexpr (std::is_same_v<T, typename ExchangeTraits::ModifyOrderResponse>) {
      handle_modify_order_response<ExchangeTraits>(arg, context, message);
    }
    else if constexpr (std::is_same_v<T, typename ExchangeTraits::CancelAllOrdersResponse>) {
      handle_cancel_all_response<ExchangeTraits>(arg, context, message);
    }
    else if constexpr (std::is_same_v<T, typename ExchangeTraits::PlaceOrderResponse>) {
      handle_place_order_response<ExchangeTraits>(arg, context, message);
    }
    else if constexpr (std::is_same_v<T, typename ExchangeTraits::CancelOrderResponse>) {
      handle_cancel_order_response<ExchangeTraits>(arg, context, message);
    }
    else if constexpr (std::is_same_v<T, typename ExchangeTraits::ApiResponse>) {
      handle_api_response<ExchangeTraits>(arg, context, message);
    }
    else if constexpr (std::is_same_v<T, typename ExchangeTraits::BalanceUpdateEnvelope>) {
      handle_balance_update<ExchangeTraits>(arg, context);
    }
    else if constexpr (std::is_same_v<T, typename ExchangeTraits::OutboundAccountPositionEnvelope>) {
      handle_account_updated<ExchangeTraits>(arg, context);
    }
    else if constexpr (std::is_same_v<T, typename ExchangeTraits::ListenKeyExpiredEvent>) {
      handle_listen_key_expired<ExchangeTraits>(arg, context);
    }
    else if constexpr (!std::is_same_v<T, std::monostate>) {
      context.logger->warn("[Dispatcher] Unhandled message type");
    }
  }, message);
}

// Handler implementations
template <typename ExchangeTraits>
void BinanceFuturesOeDispatchRouter::handle_execution_report(
    const typename ExchangeTraits::ExecutionReportResponse& report,
    const core::WsOeDispatchContext<ExchangeTraits>& context,
    const typename ExchangeTraits::WireMessage& message) {

  const auto& event = report.event;

  std::string_view dispatch_type = "8";  // Default: execution report
  if (event.execution_type == "CANCELED" && event.reject_reason != "0") {
    dispatch_type = "9";  // Cancel reject
  }

  context.app->dispatch(std::string(dispatch_type), message);

  context.order_manager->remove_pending_request(event.client_order_id);
  context.order_manager->remove_cancel_and_reorder_pair(event.client_order_id);
}

template <typename ExchangeTraits>
void BinanceFuturesOeDispatchRouter::handle_session_logon(
    const typename ExchangeTraits::SessionLogonResponse& response,
    const core::WsOeDispatchContext<ExchangeTraits>& context,
    const typename ExchangeTraits::WireMessage& message) {

  if (response.status == kHttpOK) {
    context.logger->info("[Dispatcher] session.logon successful");

    if constexpr (ExchangeTraits::requires_listen_key()) {
      const std::string user_stream_msg = context.app->create_user_data_stream_subscribe();
      if (!user_stream_msg.empty()) {
        context.app->send(user_stream_msg);
        context.logger->info("[Dispatcher] Sent userDataStream.start request");
      }
    } else {
      const std::string user_stream_msg = context.app->create_user_data_stream_subscribe();
      if (!user_stream_msg.empty()) {
        context.app->send(user_stream_msg);
      }
    }
  } else {
    if (response.error.has_value()) {
      context.logger->error("[Dispatcher] session.logon failed: status={}, error={}",
                          response.status,
                          response.error.value().message);
    }
  }
  context.app->dispatch("A", message);
}

template <typename ExchangeTraits>
void BinanceFuturesOeDispatchRouter::handle_user_subscription(
    const typename ExchangeTraits::SessionUserSubscriptionResponse& response,
    const core::WsOeDispatchContext<ExchangeTraits>& context,
    const typename ExchangeTraits::WireMessage& /*message*/) {

  if (response.status != kHttpOK) {
    context.logger->warn("[Dispatcher] UserDataStream response failed: id={}, status={}",
                        response.id,
                        response.status);
    return;
  }

  if constexpr (ExchangeTraits::requires_listen_key()) {
    if (response.result.has_value() && !response.result.value().listen_key.empty()) {
      context.app->handle_listen_key_response(response.result.value().listen_key);
      context.logger->info("[Dispatcher] Received listenKey, delegating to app for stream setup");
    } else {
      context.logger->error("[Dispatcher] UserDataStream response missing listenKey");
    }
  }
}

template <typename ExchangeTraits>
void BinanceFuturesOeDispatchRouter::handle_api_response(
    const typename ExchangeTraits::ApiResponse& response,
    const core::WsOeDispatchContext<ExchangeTraits>& context,
    const typename ExchangeTraits::WireMessage& message) {

  if (response.status != kHttpOK) {
    if (response.error.has_value()) {
      context.logger->warn("[Dispatcher] API response failed: id={}, status={}, error={}",
                          response.id,
                          response.status,
                          response.error.value().message);

      context.app->dispatch("8", message);
    }
  }
}

template <typename ExchangeTraits>
void BinanceFuturesOeDispatchRouter::handle_cancel_and_reorder_response(
    const typename ExchangeTraits::CancelAndReorderResponse& response,
    const core::WsOeDispatchContext<ExchangeTraits>& context,
    const typename ExchangeTraits::WireMessage& /*message*/) {

  if (response.status != kHttpOK && response.error.has_value()) {
    const auto& error = response.error.value();
    context.logger->warn(
        "[Dispatcher] CancelAndReorder failed: id={}, status={}, error={}",
        response.id,
        response.status,
        error.message);

    if (!error.data.has_value()) {
      return;
    }

    const auto& error_data = error.data.value();

    // Extract client order IDs
    const auto new_order_id_opt =
        core::WsOrderManager<ExchangeTraits>::extract_client_order_id(response.id);
    if (!new_order_id_opt.has_value()) {
      context.logger->error("[Dispatcher] Failed to extract client_order_id from {}",
                          response.id);
      return;
    }
    std::uint64_t new_order_id = new_order_id_opt.value();

    const auto original_order_id_opt =
        context.order_manager->get_original_order_id(new_order_id);
    if (!original_order_id_opt.has_value()) {
      context.logger->warn(
          "[Dispatcher] No cancel_and_reorder pair found for new_order_id={}",
          new_order_id);

      // Create synthetic report for NEW order
      auto synthetic_report = context.order_manager->create_synthetic_execution_report(
          response.id,
          error.code,
          error.message);
      if (synthetic_report.has_value()) {
        context.app->dispatch("8", synthetic_report.value());
      }
      return;
    }
    const auto original_order_id = original_order_id_opt.value();

    // Handle NEW order failure
    if (error_data.new_order_result != "SUCCESS") {
      context.logger->info("[Dispatcher] New order {}, creating synthetic report",
                          error_data.new_order_result);
      auto new_order_report = context.order_manager->create_synthetic_execution_report(
          response.id,
          error.code,
          error.message);
      if (new_order_report.has_value()) {
        context.app->dispatch("8", new_order_report.value());
      }
    }

    // Handle CANCEL failure
    if (error_data.cancel_result == "FAILURE" &&
        error_data.new_order_result != "SUCCESS") {
      context.logger->info("[Dispatcher] Cancel FAILURE, creating synthetic report");
      const auto orig_request_id = "ordercancel_" + std::to_string(original_order_id);
      auto cancel_report = context.order_manager->create_synthetic_execution_report(
          orig_request_id,
          error.code,
          error.message);
      if (cancel_report.has_value()) {
        context.app->dispatch("8", cancel_report.value());
      }
    } else if (error_data.cancel_result == "SUCCESS") {
      context.logger->info("[Dispatcher] Cancel SUCCESS (no synthetic report needed)");
    }

    // Cleanup
    context.order_manager->remove_pending_request(new_order_id);
    context.order_manager->remove_pending_request(original_order_id);
    context.order_manager->remove_cancel_and_reorder_pair(new_order_id);
  }
}

template <typename ExchangeTraits>
void BinanceFuturesOeDispatchRouter::handle_modify_order_response(
    const typename ExchangeTraits::ModifyOrderResponse& response,
    const core::WsOeDispatchContext<ExchangeTraits>& context,
    const typename ExchangeTraits::WireMessage& /*message*/) {

  if (response.status != kHttpOK && response.error.has_value()) {
    context.logger->warn("[Dispatcher] ModifyOrder failed: id={}, status={}, error={}",
                        response.id,
                        response.status,
                        response.error.value().message);

    auto synthetic_report = context.order_manager->create_synthetic_execution_report(
        response.id,
        response.error.value().code,
        response.error.value().message);
    if (synthetic_report.has_value()) {
      context.app->dispatch("8", synthetic_report.value());
    }
  }
}

template <typename ExchangeTraits>
void BinanceFuturesOeDispatchRouter::handle_cancel_all_response(
    const typename ExchangeTraits::CancelAllOrdersResponse& response,
    const core::WsOeDispatchContext<ExchangeTraits>& context,
    const typename ExchangeTraits::WireMessage& /*message*/) {

  if constexpr (std::is_same_v<typename ExchangeTraits::CancelAllOrdersResponse, std::monostate>) {
    // CancelAll not supported
    return;
  }

  if (response.status != kHttpOK && response.error.has_value()) {
    context.logger->warn("[Dispatcher] CancelAll failed: id={}, status={}, error={}",
                        response.id,
                        response.status,
                        response.error.value().message);

    auto synthetic_report = context.order_manager->create_synthetic_execution_report(
        response.id,
        response.error.value().code,
        response.error.value().message);
    if (synthetic_report.has_value()) {
      context.app->dispatch("8", synthetic_report.value());
    }
  }
}

template <typename ExchangeTraits>
void BinanceFuturesOeDispatchRouter::handle_place_order_response(
    const typename ExchangeTraits::PlaceOrderResponse& response,
    const core::WsOeDispatchContext<ExchangeTraits>& context,
    const typename ExchangeTraits::WireMessage& /*message*/) {

  if (response.status != kHttpOK && response.error.has_value()) {
    context.logger->debug("[Dispatcher] PlaceOrder failed: id={}, status={}, error={}",
                        response.id,
                        response.status,
                        response.error.value().message);

    auto synthetic_report = context.order_manager->create_synthetic_execution_report(
        response.id,
        response.error.value().code,
        response.error.value().message);
    if (synthetic_report.has_value()) {
      context.app->dispatch("8", synthetic_report.value());
    }
  }
}

template <typename ExchangeTraits>
void BinanceFuturesOeDispatchRouter::handle_cancel_order_response(
    const typename ExchangeTraits::CancelOrderResponse& response,
    const core::WsOeDispatchContext<ExchangeTraits>& context,
    const typename ExchangeTraits::WireMessage&) {

  if (response.status == kHttpOK) {
    context.logger->debug("[Dispatcher] CancelOrder success: id={}, orderId={}, status={}",
                        response.id,
                        response.result.order_id,
                        response.result.status);
  } else {
    context.logger->warn("[Dispatcher] CancelOrder failed: id={}, status={}",
                        response.id,
                        response.status);
  }

  const auto client_order_id_opt =
      core::WsOrderManager<ExchangeTraits>::extract_client_order_id(response.id);
  if (client_order_id_opt.has_value()) {
    context.order_manager->remove_pending_request(client_order_id_opt.value());
  }
}

template <typename ExchangeTraits>
void BinanceFuturesOeDispatchRouter::handle_balance_update(
    const typename ExchangeTraits::BalanceUpdateEnvelope& /*envelope*/,
    const core::WsOeDispatchContext<ExchangeTraits>& /*context*/) {
  // Currently disabled - can add logging if needed
  // std::ostringstream stream;
  // stream << "BalanceUpdated : " << envelope.event;
  // context.logger.debug(stream.str());
}

template <typename ExchangeTraits>
void BinanceFuturesOeDispatchRouter::handle_account_updated(
    const typename ExchangeTraits::OutboundAccountPositionEnvelope& /*envelope*/,
    const core::WsOeDispatchContext<ExchangeTraits>& /*context*/) {
  // Currently disabled - can add logging if needed
  // std::ostringstream stream;
  // stream << "AccountUpdated : " << envelope.event;
  // context.logger.debug(stream.str());
}

template <typename ExchangeTraits>
void BinanceFuturesOeDispatchRouter::handle_listen_key_expired(
    const typename ExchangeTraits::ListenKeyExpiredEvent& event,
    const core::WsOeDispatchContext<ExchangeTraits>& context) {
  context.logger->warn("[Dispatcher] listenKey expired at event_time={}, requesting new listenKey", event.event_time);

  // Request new listenKey - the dispatcher will handle reconnection via handle_user_subscription
  const std::string user_stream_msg = context.app->create_user_data_stream_subscribe();
  if (!user_stream_msg.empty()) {
    context.app->send(user_stream_msg);
    context.logger->info("[Dispatcher] Sent userDataStream.start to obtain new listenKey");
  } else {
    context.logger->error("[Dispatcher] Failed to create userDataStream.start message");
  }
}

#endif  //BINANCE_FUTURES_OE_DISPATCHER_TPP
