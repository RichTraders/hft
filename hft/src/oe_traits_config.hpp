#pragma once

#ifdef ENABLE_WEBSOCKET
#ifdef USE_FUTURES_API
#include "core/websocket/order_entry/exchanges/binance/futures/binance_futures_oe_traits.h"
using SelectedOeTraits = BinanceFuturesOeTraits;
#else
#include "core/websocket/order_entry/exchanges/binance/spot/binance_spot_oe_traits.h"
using SelectedOeTraits = BinanceSpotOeTraits;
#endif
#else
// FIX protocol fallback
struct SelectedOeTraits {
  static constexpr bool supports_cancel_and_reorder() { return false; }
  static constexpr bool supports_position_side() { return false; }
};
#endif