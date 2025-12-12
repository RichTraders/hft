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

#ifndef EXCHANGE_INFO_RESPONSE_H
#define EXCHANGE_INFO_RESPONSE_H
#include <glaze/glaze.hpp>

#include "api_response.h"

namespace schema {
struct ExchangeFilter {
  std::string filter_type;

  struct glaze {
    using T = ExchangeFilter;
    static constexpr auto value = glz::object("filterType", &T::filter_type);
  };
};

struct SymbolFilter {
  std::string filter_type;

  std::optional<std::string> min_price;  // PRICE_FILTER
  std::optional<std::string> max_price;
  std::optional<std::string> tick_size;

  std::optional<std::string> min_qty;  // LOT_SIZE
  std::optional<std::string> max_qty;
  std::optional<std::string> step_size;

  // clang-format off
  struct glaze {
    using T = SymbolFilter;
    static constexpr auto value = glz::object(
      "filterType", &T::filter_type,

      "minPrice",   &T::min_price,
      "maxPrice",   &T::max_price,
      "tickSize",   &T::tick_size,

      "minQty",     &T::min_qty,
      "maxQty",     &T::max_qty,
      "stepSize",   &T::step_size
    );
  };
  // clang-format on
};

struct SymbolInfo {
  std::string symbol;
  std::string status;

  std::string base_asset;
  std::uint32_t base_asset_precision{};

  std::string quote_asset;
  std::uint32_t quote_precision{};
  std::uint32_t quote_asset_precision{};

  std::uint32_t base_commission_precision{};
  std::uint32_t quote_commission_precision{};

  std::vector<std::string> order_types;

  bool iceberg_allowed{};
  bool oco_allowed{};
  bool oto_allowed{};
  bool quote_order_qty_market_allowed{};
  bool allow_trailing_stop{};
  bool cancel_replace_allowed{};
  bool amend_allowed{};
  bool peg_instructions_allowed{};
  bool is_spot_trading_allowed{};
  bool is_margin_trading_allowed{};

  std::vector<SymbolFilter> filters;

  std::vector<std::string> permissions;
  std::vector<std::vector<std::string>> permission_sets;

  std::string default_self_trade_prevention_mode;
  std::vector<std::string> allowed_self_trade_prevention_modes;

  // clang-format off
  struct glaze {
    using T = SymbolInfo;
    static constexpr auto value = glz::object(
      "symbol",                    &T::symbol,
      "status",                    &T::status,

      "baseAsset",                 &T::base_asset,
      "baseAssetPrecision",        &T::base_asset_precision,

      "quoteAsset",                &T::quote_asset,
      "quotePrecision",            &T::quote_precision,
      "quoteAssetPrecision",       &T::quote_asset_precision,

      "baseCommissionPrecision",   &T::base_commission_precision,
      "quoteCommissionPrecision",  &T::quote_commission_precision,

      "orderTypes",                &T::order_types,

      "icebergAllowed",            &T::iceberg_allowed,
      "ocoAllowed",                &T::oco_allowed,
      "otoAllowed",                &T::oto_allowed,
      "quoteOrderQtyMarketAllowed",&T::quote_order_qty_market_allowed,
      "allowTrailingStop",         &T::allow_trailing_stop,
      "cancelReplaceAllowed",      &T::cancel_replace_allowed,
      "amendAllowed",              &T::amend_allowed,
      "pegInstructionsAllowed",    &T::peg_instructions_allowed,
      "isSpotTradingAllowed",      &T::is_spot_trading_allowed,
      "isMarginTradingAllowed",    &T::is_margin_trading_allowed,

      "filters",                   &T::filters,

      "permissions",               &T::permissions,
      "permissionSets",            &T::permission_sets,

      "defaultSelfTradePreventionMode", &T::default_self_trade_prevention_mode,
      "allowedSelfTradePreventionModes",&T::allowed_self_trade_prevention_modes
    );
  };
  // clang-format on
};

struct ExchangeInfoResult {
  std::string timezone;
  std::uint64_t server_time{};

  //Some bug appears if rate limit field exists. Comment in it.
  //std::optional<std::vector<RateLimit>> rate_limits;
  std::vector<ExchangeFilter> exchange_filters;
  std::vector<SymbolInfo> symbols;

  // clang-format off
  struct glaze {
    using T = ExchangeInfoResult;
    static constexpr auto value = glz::object(
      "timezone",        &T::timezone,
      "serverTime",      &T::server_time,
     // "rateLimits",      &T::rate_limits,
      "exchangeFilters", &T::exchange_filters,
      "symbols",         &T::symbols
    );
  };
  // clang-format on
};

struct ExchangeInfoResponse {
  std::string id;
  std::uint32_t status{};
  ExchangeInfoResult result;
  std::optional<std::vector<RateLimit>> rate_limits;

  // clang-format off
  struct glaze {
    using T = ExchangeInfoResponse;
    static constexpr auto value = glz::object(
      "id",         &T::id,
      "status",     &T::status,
      "result",     &T::result,
      "rateLimits", &T::rate_limits
    );
  };
  // clang-format on
};

}  // namespace schema
#endif  //EXCHANGE_INFO_RESPONSE_H
