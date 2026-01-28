#ifndef OE_TRAITS_CONFIG_HPP
#define OE_TRAITS_CONFIG_HPP

// Check if SelectedOeTraits is already defined (e.g., by generated test header)
#ifndef SELECTED_OE_TRAITS_DEFINED

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

#define SELECTED_OE_TRAITS_DEFINED
#endif

#endif  // OE_TRAITS_CONFIG_HPP