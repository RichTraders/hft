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

#ifndef BINANCE_FUTURES_FORMATTER_H
#define BINANCE_FUTURES_FORMATTER_H

struct BinanceFuturesFormatter {
  static std::string format_depth_stream(std::string_view symbol) {
    return std::format("{0}@depth", symbol);
  }

  static std::string format_trade_stream(std::string_view symbol) {
    return std::format("{0}@aggTrade", symbol);
  }
};
#endif  //BINANCE_FUTURES_FORMATTER_H
