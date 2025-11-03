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

#ifndef STRATEGIES_HPP
#define STRATEGIES_HPP

// Include all strategy implementations here
// This ensures that all strategies are registered with the StrategyDispatch

#include "liquid_taker.h"
#include "market_maker.h"
namespace trading {
inline void register_all_strategies() {
  register_market_maker_strategy();
  register_liquid_taker_strategy();
}
}  // namespace trading

#endif  // STRATEGIES_HPP
