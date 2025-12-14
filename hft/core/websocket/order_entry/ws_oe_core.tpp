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

//#include "exchanges/binance/futures/futures_ws_oe_decoder.h"
#include "spot_ws_oe_decoder.h"

namespace core {

template <OeExchangeTraits Traits, typename DecoderType>
std::string WsOeCore<Traits, DecoderType>::create_log_on_message(const std::string& signature,
    const std::string& timestamp) const {
  return encoder_.create_log_on_message(signature, timestamp);
}

template <OeExchangeTraits Traits, typename DecoderType>
std::string WsOeCore<Traits, DecoderType>::create_log_out_message() const {
  return encoder_.create_log_out_message();
}

template <OeExchangeTraits Traits, typename DecoderType>
std::string WsOeCore<Traits, DecoderType>::create_heartbeat_message() const {
  return encoder_.create_heartbeat_message();
}

template <OeExchangeTraits Traits, typename DecoderType>
std::string WsOeCore<Traits, DecoderType>::create_user_data_stream_subscribe() const {
  return encoder_.create_user_data_stream_subscribe();
}

template <OeExchangeTraits Traits, typename DecoderType>
std::string WsOeCore<Traits, DecoderType>::create_user_data_stream_unsubscribe() const {
  return encoder_.create_user_data_stream_unsubscribe();
}

template <OeExchangeTraits Traits, typename DecoderType>
std::string WsOeCore<Traits, DecoderType>::create_user_data_stream_ping() const {
  return encoder_.create_user_data_stream_ping();
}

template <OeExchangeTraits Traits, typename DecoderType>
std::string WsOeCore<Traits, DecoderType>::create_order_message(
    const trading::NewSingleOrderData& order) const {
  return encoder_.create_order_message(order);
}

template <OeExchangeTraits Traits, typename DecoderType>
std::string WsOeCore<Traits, DecoderType>::create_cancel_order_message(
    const trading::OrderCancelRequest& cancel) const {
  return encoder_.create_cancel_order_message(cancel);
}

template <OeExchangeTraits Traits, typename DecoderType>
std::string WsOeCore<Traits, DecoderType>::create_cancel_and_reorder_message(
    const trading::OrderCancelAndNewOrderSingle& replace) const {
  return encoder_.create_cancel_and_reorder_message(replace);
}

template <OeExchangeTraits Traits, typename DecoderType>
std::string WsOeCore<Traits, DecoderType>::create_order_all_cancel(
    const trading::OrderMassCancelRequest& request) const {
  return encoder_.create_order_all_cancel(request);
}

template <OeExchangeTraits Traits, typename DecoderType>
trading::ExecutionReport* WsOeCore<Traits, DecoderType>::create_execution_report_message(
    const WireExecutionReport& msg) const {
  return mapper_.to_execution_report(msg);
}

template <OeExchangeTraits Traits, typename DecoderType>
trading::OrderCancelReject* WsOeCore<Traits, DecoderType>::create_order_cancel_reject_message(
    const WireCancelReject& msg) const {
  return mapper_.to_cancel_reject(msg);
}

template <OeExchangeTraits Traits, typename DecoderType>
trading::OrderMassCancelReport*
WsOeCore<Traits, DecoderType>::create_order_mass_cancel_report_message(
    const WireMassCancelReport& msg) const {
  return mapper_.to_mass_cancel_report(msg);
}

template <OeExchangeTraits Traits, typename DecoderType>
trading::OrderReject WsOeCore<Traits, DecoderType>::create_reject_message(
    const WireReject& msg) const {
  return mapper_.to_reject(msg);
}

template <OeExchangeTraits Traits, typename DecoderType>
typename WsOeCore<Traits, DecoderType>::WireMessage WsOeCore<Traits, DecoderType>::decode(std::string_view payload) const {
  return decoder_.decode(payload);
}

}  // namespace core
