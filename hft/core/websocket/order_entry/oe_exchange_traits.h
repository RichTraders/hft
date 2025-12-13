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

#ifndef OE_EXCHANGE_TRAITS_H
#define OE_EXCHANGE_TRAITS_H

template <typename T>
concept OeExchangeTraits = requires {
  { T::exchange_name() } -> std::convertible_to<std::string_view>;
  { T::market_type() } -> std::convertible_to<std::string_view>;

  { T::get_api_host() } -> std::convertible_to<std::string_view>;
  { T::get_api_endpoint_path() } -> std::convertible_to<std::string_view>;
  { T::get_api_port() } -> std::same_as<int>;
  { T::use_ssl() } -> std::same_as<bool>;

  { T::get_stream_host() } -> std::convertible_to<std::string_view>;
  { T::get_stream_endpoint_path() } -> std::convertible_to<std::string_view>;
  { T::get_stream_port() } -> std::same_as<int>;

  { T::requires_listen_key() } -> std::same_as<bool>;
  { T::requires_stream_transport() } -> std::same_as<bool>;
  { T::requires_signature_logon() } -> std::same_as<bool>;
  { T::supports_position_side() } -> std::same_as<bool>;
  { T::supports_reduce_only() } -> std::same_as<bool>;
  { T::supports_cancel_and_reorder() } -> std::same_as<bool>;

  typename T::DispatchRouter;
  typename T::Encoder;
  typename T::Mapper;
  typename T::WireMessage;
  typename T::ExecutionReportResponse;
  typename T::PlaceOrderResponse;
  typename T::CancelOrderResponse;
  typename T::ApiResponse;
  typename T::SessionLogonResponse;
  typename T::SessionUserSubscriptionResponse;
};

#endif  // OE_EXCHANGE_TRAITS_H
