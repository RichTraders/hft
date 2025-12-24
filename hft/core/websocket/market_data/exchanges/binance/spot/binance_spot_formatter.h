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

#ifndef BINANCE_SPOT_FORMATTER_H
#define BINANCE_SPOT_FORMATTER_H

#include <format>
#include <string>
#include <string_view>

struct BinanceSpotFormatter {
  static std::string format_depth_stream(std::string_view symbol) {
    return std::format("{0}@depth@100ms", symbol);
  }

  static std::string format_trade_stream(std::string_view symbol) {
    return std::format("{0}@trade", symbol);
  }
};
#endif  //BINANCE_SPOT_FORMATTER_H
