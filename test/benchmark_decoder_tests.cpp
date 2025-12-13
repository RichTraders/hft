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

#include <benchmark/benchmark.h>
#include <fstream>
#include <sstream>
#include "logger.h"
#include "websocket/order_entry/spot_ws_oe_decoder.h"

// Helper function to load test data from file
std::string load_test_data(const std::string& filename) {
  std::string path = "data/execution_reports/" + filename;
  std::ifstream file(path);
  if (!file.is_open()) {
    // Fallback to inline JSON if file doesn't exist
    return "";
  }
  return std::string(std::istreambuf_iterator<char>(file),
                     std::istreambuf_iterator<char>());
}

// Fallback execution report JSON for when user data is not available
static const char* kFallbackExecutionReportJson = R"({
  "subscriptionId": 1,
  "event": {
    "e": "executionReport",
    "E": 1699564800000,
    "s": "BTCUSDT",
    "c": "1234567890",
    "S": "BUY",
    "o": "LIMIT",
    "f": "GTC",
    "q": "0.50000",
    "p": "50000.00",
    "P": "0.00",
    "F": "0.00000",
    "g": -1,
    "C": "",
    "x": "TRADE",
    "X": "PARTIALLY_FILLED",
    "r": "NONE",
    "i": 9876543210,
    "l": "0.25000",
    "z": "0.25000",
    "L": "50000.00",
    "n": "0.00125",
    "N": "BTC",
    "T": 1699564800000,
    "t": 555555,
    "v": 0,
    "I": 12345,
    "w": true,
    "m": false,
    "M": false,
    "O": 1699564799000,
    "Z": "12500.00",
    "Y": "12500.00",
    "Q": "0.00",
    "W": 1699564800000,
    "V": "NONE"
  }
})";

static const char* kFallbackSessionLogonJson = R"({
  "id": "login_1699564800000",
  "status": 200,
  "result": {
    "apiKey": "test_api_key",
    "authorizedSince": 1699564800000,
    "connectedSince": 1699564799000,
    "returnRateLimits": true,
    "serverTime": 1699564800000,
    "userDataStream": true
  },
  "rateLimits": [
    {
      "rateLimitType": "REQUEST_WEIGHT",
      "interval": "MINUTE",
      "intervalNum": 1,
      "limit": 6000,
      "count": 1
    }
  ]
})";

static const char* kFallbackPlaceOrderJson = R"({
  "id": "place_order_123",
  "status": 200,
  "result": {
    "symbol": "BTCUSDT",
    "orderId": 12345,
    "orderListId": -1,
    "clientOrderId": "9999999999",
    "transactTime": 1699564800000,
    "price": "50000.00",
    "origQty": "1.50000",
    "executedQty": "0.00000",
    "cummulativeQuoteQty": "0.00",
    "status": "NEW",
    "timeInForce": "GTC",
    "type": "LIMIT",
    "side": "BUY",
    "selfTradePreventionMode": "NONE"
  },
  "rateLimits": []
})";

static void BM_DecodeExecutionReport(benchmark::State& state) {
  auto logger = std::make_unique<common::Logger>();
  logger->clearSink();
  auto producer = logger->make_producer();
  auto decoder = std::make_unique<core::SpotWsOeDecoder>(producer);

  // Try to load user-provided data, fallback to inline JSON
  std::string json = load_test_data("execution_report_trade.json");
  if (json.empty()) {
    json = kFallbackExecutionReportJson;
  }

  for (auto _ : state) {
    auto result = decoder->decode(json);
    benchmark::DoNotOptimize(result);
  }

  state.SetItemsProcessed(state.iterations());
  state.SetBytesProcessed(state.iterations() * json.size());
}
BENCHMARK(BM_DecodeExecutionReport);

static void BM_DecodeSessionLogon(benchmark::State& state) {
  auto logger = std::make_unique<common::Logger>();
  logger->clearSink();
  auto producer = logger->make_producer();
  auto decoder = std::make_unique<core::SpotWsOeDecoder>(producer);

  std::string json = load_test_data("session_logon_success.json");
  if (json.empty()) {
    json = kFallbackSessionLogonJson;
  }

  for (auto _ : state) {
    auto result = decoder->decode(json);
    benchmark::DoNotOptimize(result);
  }

  state.SetItemsProcessed(state.iterations());
  state.SetBytesProcessed(state.iterations() * json.size());
}
BENCHMARK(BM_DecodeSessionLogon);

static void BM_DecodePlaceOrderResponse(benchmark::State& state) {
  auto logger = std::make_unique<common::Logger>();
  logger->clearSink();
  auto producer = logger->make_producer();
  auto decoder = std::make_unique<core::SpotWsOeDecoder>(producer);

  std::string json = load_test_data("place_order_response_ack.json");
  if (json.empty()) {
    json = kFallbackPlaceOrderJson;
  }

  for (auto _ : state) {
    auto result = decoder->decode(json);
    benchmark::DoNotOptimize(result);
  }

  state.SetItemsProcessed(state.iterations());
  state.SetBytesProcessed(state.iterations() * json.size());
}
BENCHMARK(BM_DecodePlaceOrderResponse);

static void BM_DecodeExecutionReport_Complex(benchmark::State& state) {
  auto logger = std::make_unique<common::Logger>();
  logger->clearSink();
  auto producer = logger->make_producer();
  auto decoder = std::make_unique<core::SpotWsOeDecoder>(producer);

  // Use the most complex execution report (TRADE with all fields)
  std::string json = load_test_data("execution_report_filled.json");
  if (json.empty()) {
    json = kFallbackExecutionReportJson;
  }

  for (auto _ : state) {
    auto result = decoder->decode(json);
    benchmark::DoNotOptimize(result);
  }

  state.SetItemsProcessed(state.iterations());
  state.SetBytesProcessed(state.iterations() * json.size());
}
BENCHMARK(BM_DecodeExecutionReport_Complex);