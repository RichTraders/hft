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

#ifndef EXCHANGE_TRAITS_H
#define EXCHANGE_TRAITS_H

template <typename T>
concept ExchangeTraits = requires(std::string_view payload) {
  { T::exchange_name() } -> std::convertible_to<std::string_view>;
  { T::market_type() } -> std::convertible_to<std::string_view>;

  { T::get_api_host() } -> std::convertible_to<std::string_view>;
  { T::get_stream_host() } -> std::convertible_to<std::string_view>;
  { T::get_api_endpoint_path() } -> std::convertible_to<std::string_view>;
  { T::get_stream_endpoint_path() } -> std::convertible_to<std::string_view>;
  { T::get_api_port() } -> std::same_as<int>;
  { T::use_ssl() } -> std::same_as<bool>;

  typename T::DepthResponse;
  typename T::TradeEvent;
  typename T::DepthSnapshot;
  typename T::ApiResponse;
  typename T::ExchangeInfoResponse;

  typename T::SbeOps;
  typename T::Classifier;
  typename T::Formatter;
  typename T::Encoder;
  typename T::MdDomainConverter;

  { T::supports_json() } -> std::same_as<bool>;
  { T::supports_sbe() } -> std::same_as<bool>;

  { T::is_depth_message(payload) } -> std::same_as<bool>;
  { T::is_trade_message(payload) } -> std::same_as<bool>;
  { T::is_snapshot_message(payload) } -> std::same_as<bool>;
};

#endif  //EXCHANGE_TRAITS_H
