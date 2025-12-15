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

#ifndef FUTURES_EXCHANGE_INFO_RESPONSE_H
#define FUTURES_EXCHANGE_INFO_RESPONSE_H
#include <glaze/glaze.hpp>

#include "api_response.h"

namespace schema {
namespace futures {

struct SymbolFilter {
  std::string filter_type;

  // PRICE_FILTER
  std::optional<std::string> min_price;
  std::optional<std::string> max_price;
  std::optional<std::string> tick_size;

  // LOT_SIZE, MARKET_LOT_SIZE
  std::optional<std::string> min_qty;
  std::optional<std::string> max_qty;
  std::optional<std::string> step_size;

  // MAX_NUM_ORDERS, MAX_NUM_ALGO_ORDERS
  std::optional<int> limit;

  // MIN_NOTIONAL
  std::optional<std::string> notional;

  // PERCENT_PRICE
  std::optional<std::string> multiplier_up;
  std::optional<std::string> multiplier_down;
  std::optional<std::string> multiplier_decimal;

  // POSITION_RISK_CONTROL
  std::optional<std::string> position_control_side;

  // clang-format off
  struct glaze {
    using T = SymbolFilter;
    static constexpr auto value = glz::object(
      "filterType",          &T::filter_type,

      "minPrice",            &T::min_price,
      "maxPrice",            &T::max_price,
      "tickSize",            &T::tick_size,

      "minQty",              &T::min_qty,
      "maxQty",              &T::max_qty,
      "stepSize",            &T::step_size,

      "limit",               &T::limit,

      "notional",            &T::notional,

      "multiplierUp",        &T::multiplier_up,
      "multiplierDown",      &T::multiplier_down,
      "multiplierDecimal",   &T::multiplier_decimal,

      "positionControlSide", &T::position_control_side
    );
  };
  // clang-format on
};

struct AssetInfo {
  std::string asset;
  bool margin_available{false};
  std::optional<std::string> auto_asset_exchange;

  // clang-format off
  struct glaze {
    using T = AssetInfo;
    static constexpr auto value = glz::object(
      "asset",             &T::asset,
      "marginAvailable",   &T::margin_available,
      "autoAssetExchange", &T::auto_asset_exchange
    );
  };
  // clang-format on
};

struct SymbolInfo {
  std::string symbol;
  std::string pair;
  std::string contract_type;
  std::uint64_t delivery_date{};
  std::uint64_t onboard_date{};
  std::string status;
  std::string maint_margin_percent;
  std::string required_margin_percent;

  std::string base_asset;
  std::string quote_asset;
  std::string margin_asset;

  std::int32_t price_precision{};
  std::int32_t quantity_precision{};
  std::int32_t base_asset_precision{};
  std::int32_t quote_precision{};

  std::string underlying_type;
  std::vector<std::string> underlying_sub_type;

  std::optional<int> settle_plan;
  std::optional<std::string> trigger_protect;
  std::optional<std::string> liquidation_fee;
  std::optional<std::string> market_take_bound;
  std::optional<int> max_move_order_limit;

  std::vector<SymbolFilter> filters;
  std::vector<std::string> order_types;
  std::vector<std::string> time_in_force;
  std::optional<std::vector<std::string>> permission_sets;

  // clang-format off
  struct glaze {
    using T = SymbolInfo;
    static constexpr auto value = glz::object(
      "symbol",                &T::symbol,
      "pair",                  &T::pair,
      "contractType",          &T::contract_type,
      "deliveryDate",          &T::delivery_date,
      "onboardDate",           &T::onboard_date,
      "status",                &T::status,
      "maintMarginPercent",    &T::maint_margin_percent,
      "requiredMarginPercent", &T::required_margin_percent,

      "baseAsset",             &T::base_asset,
      "quoteAsset",            &T::quote_asset,
      "marginAsset",           &T::margin_asset,

      "pricePrecision",        &T::price_precision,
      "quantityPrecision",     &T::quantity_precision,
      "baseAssetPrecision",    &T::base_asset_precision,
      "quotePrecision",        &T::quote_precision,

      "underlyingType",        &T::underlying_type,
      "underlyingSubType",     &T::underlying_sub_type,

      "settlePlan",            &T::settle_plan,
      "triggerProtect",        &T::trigger_protect,
      "liquidationFee",        &T::liquidation_fee,
      "marketTakeBound",       &T::market_take_bound,
      "maxMoveOrderLimit",     &T::max_move_order_limit,

      "filters",               &T::filters,
      "orderTypes",            &T::order_types,
      "OrderType",             &T::order_types,
      "timeInForce",           &T::time_in_force,
      "permissionSets",        &T::permission_sets
    );
  };
  // clang-format on
};

// HTTP REST API response for /fapi/v1/exchangeInfo
struct ExchangeInfoHttpResponse {
  std::string timezone;
  std::uint64_t server_time{};
  std::optional<std::string> futures_type;
  std::optional<std::vector<RateLimit>> rate_limits;
  std::vector<AssetInfo> assets;
  std::vector<SymbolInfo> symbols;

  // clang-format off
  struct glaze {
    using T = ExchangeInfoHttpResponse;
    static constexpr auto value = glz::object(
      "timezone",       &T::timezone,
      "serverTime",     &T::server_time,
      "futuresType",    &T::futures_type,
      "rateLimits",     &T::rate_limits,
      "exchangeFilters",&T::rate_limits,
      "assets",         &T::assets,
      "symbols",        &T::symbols
    );
  };
  // clang-format on
};

}  // namespace futures
}  // namespace schema
#endif  // FUTURES_EXCHANGE_INFO_RESPONSE_H
