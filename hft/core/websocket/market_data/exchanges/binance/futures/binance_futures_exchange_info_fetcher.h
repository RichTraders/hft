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

#ifndef FUTURES_EXCHANGE_INFO_FETCHER_H
#define FUTURES_EXCHANGE_INFO_FETCHER_H

#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <glaze/glaze.hpp>

#include "../../../../../http/http_client.h"
#include "common/logger.h"
#include "market_data.h"
#include "schema/futures/response/exchange_info_response.h"

namespace core::http {

class BinanceFuturesExchangeInfoFetcher {
 public:
  explicit BinanceFuturesExchangeInfoFetcher(const common::Logger::Producer& logger)
      : logger_(logger) {}

  std::optional<InstrumentInfo> fetch(const std::string& symbol = "") {
    static constexpr std::string_view kBaseUrl =
        "https://fapi.binance.com/fapi/v1/exchangeInfo";

    std::string url{kBaseUrl};
    if (!symbol.empty()) {
      url += "?symbol=" + symbol;
    }

    logger_.info("[FuturesExchangeInfoFetcher] Fetching from: {}", url);

    const HttpClient client;
    auto response = client.get(url);

    if (!response.ok()) {
      logger_.error(
          "[FuturesExchangeInfoFetcher] HTTP request failed: status={}, "
          "error={}",
          response.status_code,
          response.error);
      return std::nullopt;
    }

    schema::futures::ExchangeInfoHttpResponse exchange_info;
    const auto parse_result = glz::read_json(exchange_info, response.body);
    if (parse_result) {
      logger_.error("[FuturesExchangeInfoFetcher] Failed to parse response: {}",
          glz::format_error(parse_result, response.body));
      return std::nullopt;
    }

    return convert_to_instrument_info(exchange_info, symbol);
  }

 private:
  InstrumentInfo convert_to_instrument_info(
      const schema::futures::ExchangeInfoHttpResponse& exchange_info,
      const std::string& filter_symbol) {
    InstrumentInfo info;
    info.instrument_req_id = "futures_http";

    auto parse_or_default = [](const std::optional<std::string>& str,
                                double default_value = 0.0) -> double {
      if (!str || str->empty())
        return default_value;
      const char* begin = str->c_str();
      char* end = nullptr;
      const double data = std::strtod(begin, &end);
      if (end == begin) {
        return default_value;
      }
      return data;
    };

    for (const auto& sym : exchange_info.symbols) {
      if (!filter_symbol.empty() && sym.symbol != filter_symbol) {
        continue;
      }

      InstrumentInfo::RelatedSymT related{};
      related.symbol = sym.symbol;
      related.currency = sym.quote_asset;

      const schema::futures::SymbolFilter* lot_filter = nullptr;
      const schema::futures::SymbolFilter* mlot_filter = nullptr;
      const schema::futures::SymbolFilter* price_filter = nullptr;

      for (const auto& filter : sym.filters) {
        if (filter.filter_type == "LOT_SIZE") {
          lot_filter = &filter;
        } else if (filter.filter_type == "MARKET_LOT_SIZE") {
          mlot_filter = &filter;
        } else if (filter.filter_type == "PRICE_FILTER") {
          price_filter = &filter;
        }
      }

      if (lot_filter) {
        related.min_trade_vol = parse_or_default(lot_filter->min_qty, 0.0);
        related.max_trade_vol = parse_or_default(lot_filter->max_qty, 0.0);
        related.min_qty_increment =
            parse_or_default(lot_filter->step_size, 0.0);
      }

      if (mlot_filter) {
        related.market_min_trade_vol =
            parse_or_default(mlot_filter->min_qty, related.min_trade_vol);
        related.market_max_trade_vol =
            parse_or_default(mlot_filter->max_qty, related.max_trade_vol);
        related.market_min_qty_increment =
            parse_or_default(mlot_filter->step_size, related.min_qty_increment);
      } else {
        related.market_min_trade_vol = related.min_trade_vol;
        related.market_max_trade_vol = related.max_trade_vol;
        related.market_min_qty_increment = related.min_qty_increment;
      }

      if (price_filter) {
        constexpr double kTickSize = 0.00001;
        related.min_price_increment =
            parse_or_default(price_filter->tick_size, kTickSize);
      }

      info.symbols.push_back(std::move(related));
    }

    info.no_related_sym = static_cast<int>(info.symbols.size());
    return info;
  }

  const common::Logger::Producer& logger_;
};

}  // namespace core::http

#endif  // FUTURES_EXCHANGE_INFO_FETCHER_H
