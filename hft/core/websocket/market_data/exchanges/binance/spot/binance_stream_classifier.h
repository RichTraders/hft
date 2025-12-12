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

#ifndef HFT_BINANCE_STREAM_CLASSIFIER_H
#define HFT_BINANCE_STREAM_CLASSIFIER_H

struct BinanceStreamClassifier {
  static bool is_depth(std::string_view payload) {
    return payload.find("@depth") != std::string_view::npos;
  }
  static bool is_trade(std::string_view payload) {
    return payload.find("@trade") != std::string_view::npos;
  }
  static bool is_snapshot(std::string_view payload) {
    return payload.find("snapshot") != std::string_view::npos;
  }
  static bool is_exchange_info(std::string_view payload) {
    return payload.find("exchangeInfo") != std::string_view::npos;
  }
};

#endif  //HFT_BINANCE_STREAM_CLASSIFIER_H
