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

#include <algorithm>
#include <fstream>
#include <numeric>
#include <string>
#include <vector>

// Must include strategy_config.hpp first to get SelectedStrategy
#include "strategy_config.hpp"

#include "common/ini_config.hpp"
#include "common/logger.h"
#include "common/memory_pool.hpp"
#include "common/performance.h"
#include "core/market_data.h"
#include "core/order_entry.h"
#include "core/response_manager.h"
#include "core/websocket/market_data/exchanges/binance/futures/binance_futures_traits.h"
#include "core/websocket/market_data/json_md_decoder.hpp"
#include "core/websocket/market_data/ws_md_core.h"
#include "core/websocket/order_entry/exchanges/binance/futures/binance_futures_oe_encoder.hpp"
#include "order_book.hpp"
#include "trade_engine.hpp"

struct BenchmarkStats {
  std::vector<uint64_t> samples;

  void record(uint64_t cycles) { samples.push_back(cycles); }

  void report(const char* name) const {
    if (samples.empty()) {
      printf("%s: no samples\n", name);
      return;
    }

    auto sorted = samples;
    std::sort(sorted.begin(), sorted.end());

    uint64_t sum = std::accumulate(sorted.begin(), sorted.end(), 0ULL);
    size_t n = sorted.size();

    printf("%-20s n=%-6zu avg=%-8llu p50=%-8llu p99=%-8llu min=%-8llu max=%-8llu\n",
        name,
        n,
        static_cast<unsigned long long>(sum / n),
        static_cast<unsigned long long>(sorted[n / 2]),
        static_cast<unsigned long long>(sorted[n * 99 / 100]),
        static_cast<unsigned long long>(sorted.front()),
        static_cast<unsigned long long>(sorted.back()));
  }
};

// MockOrderGateway - encodes orders without network send
template <typename Strategy>
class MockOrderGateway {
 public:
  explicit MockOrderGateway(const common::Logger::Producer& logger)
      : logger_(logger), encoder_(logger_) {}

  void init_trade_engine(trading::TradeEngine<Strategy>* trade_engine) {
    trade_engine_ = trade_engine;
  }

  void order_request(const trading::RequestCommon& request) {
    const auto start = common::rdtsc();

    switch (request.req_type) {
      case trading::ReqeustType::kNewSingleOrderData: {
        const trading::NewSingleOrderData order_data{
            .cl_order_id = request.cl_order_id,
            .symbol = request.symbol,
            .side = trading::from_common_side(request.side),
            .order_qty = request.order_qty,
            .ord_type = request.ord_type,
            .price = request.price,
            .time_in_force = request.time_in_force,
            .self_trade_prevention_mode = request.self_trade_prevention_mode,
            .position_side = request.position_side};
        encoded_message_ = encoder_.create_order_message(order_data);
        new_order_count_++;
        break;
      }
      case trading::ReqeustType::kOrderCancelRequest: {
        const trading::OrderCancelRequest cancel{
            .cl_order_id = request.cl_order_id,
            .orig_cl_order_id = request.orig_cl_order_id,
            .symbol = request.symbol,
            .position_side = request.position_side};
        encoded_message_ = encoder_.create_cancel_order_message(cancel);
        cancel_count_++;
        break;
      }
      case trading::ReqeustType::kOrderCancelRequestAndNewOrderSingle: {
        const trading::OrderCancelAndNewOrderSingle replace{
            .order_cancel_request_and_new_order_single_mode = 1,
            .cancel_new_order_id = request.cl_cancel_order_id,
            .cl_new_order_id = request.cl_order_id,
            .cl_origin_order_id = request.orig_cl_order_id,
            .symbol = request.symbol,
            .side = trading::from_common_side(request.side),
            .order_qty = request.order_qty,
            .ord_type = request.ord_type,
            .price = request.price,
            .time_in_force = request.time_in_force,
            .self_trade_prevention_mode = request.self_trade_prevention_mode,
            .position_side = request.position_side};
        encoded_message_ = encoder_.create_cancel_and_reorder_message(replace);
        replace_count_++;
        break;
      }
      case trading::ReqeustType::kOrderModify: {
        const trading::OrderModifyRequest modify{
            .order_id = request.orig_cl_order_id,
            .symbol = request.symbol,
            .side = trading::from_common_side(request.side),
            .price = request.price,
            .order_qty = request.order_qty,
            .position_side = request.position_side};
        encoded_message_ = encoder_.create_modify_order_message(modify);
        modify_count_++;
        break;
      }
      default:
        break;
    }

    encode_cycles_.push_back(common::rdtsc() - start);
  }

  void report() const {
    size_t total = new_order_count_ + cancel_count_ + replace_count_ + modify_count_;
    printf("\n=== Order Encoding Stats ===\n");
    printf("Orders: new=%zu, cancel=%zu, replace=%zu, modify=%zu (total=%zu)\n",
        new_order_count_, cancel_count_, replace_count_, modify_count_, total);

    if (!encode_cycles_.empty()) {
      auto sorted = encode_cycles_;
      std::sort(sorted.begin(), sorted.end());
      size_t n = sorted.size();
      uint64_t sum = std::accumulate(sorted.begin(), sorted.end(), 0ULL);
      printf("ENCODE               n=%-6zu avg=%-8llu p50=%-8llu p99=%-8llu min=%-8llu max=%-8llu\n",
          n,
          static_cast<unsigned long long>(sum / n),
          static_cast<unsigned long long>(sorted[n / 2]),
          static_cast<unsigned long long>(sorted[n * 99 / 100]),
          static_cast<unsigned long long>(sorted.front()),
          static_cast<unsigned long long>(sorted.back()));
    }
  }

 private:
  const common::Logger::Producer& logger_;
  core::BinanceFuturesOeEncoder encoder_;
  trading::TradeEngine<Strategy>* trade_engine_ = nullptr;

  std::string encoded_message_;
  mutable std::vector<uint64_t> encode_cycles_;

  size_t new_order_count_ = 0;
  size_t cancel_count_ = 0;
  size_t replace_count_ = 0;
  size_t modify_count_ = 0;
};

std::vector<std::string> read_all_lines(const std::string& filename) {
  std::vector<std::string> lines;
  std::ifstream file(filename);
  if (!file.is_open()) {
    fprintf(stderr, "Failed to open file: %s\n", filename.c_str());
    return lines;
  }

  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty()) {
      lines.push_back(std::move(line));
    }
  }
  return lines;
}

using TestStrategy = SelectedStrategy;
using TestTradeEngine = trading::TradeEngine<TestStrategy>;
using TestOrderBook = trading::MarketOrderBook<TestStrategy>;
using TestMockOrderGateway = MockOrderGateway<TestStrategy>;

void benchmark_full_pipeline(const std::vector<std::string>& lines,
    common::Logger* logger) {
  using MdCore = core::WsMdCore<BinanceFuturesTraits, core::JsonMdDecoder>;
  using WireMessage = MdCore::WireMessage;

  // Setup pools
  auto market_update_pool = std::make_unique<common::MemoryPool<MarketUpdateData>>(4096);
  auto market_data_pool = std::make_unique<common::MemoryPool<MarketData>>(65536);
  auto execution_report_pool = std::make_unique<common::MemoryPool<trading::ExecutionReport>>(1024);
  auto order_cancel_reject_pool = std::make_unique<common::MemoryPool<trading::OrderCancelReject>>(1024);
  auto order_mass_cancel_report_pool = std::make_unique<common::MemoryPool<trading::OrderMassCancelReport>>(1024);

  // Setup config
  common::TradeEngineCfgHashMap cfg;
  common::RiskCfg risk = {
      .max_order_size_ = common::Qty{1000.},
      .max_position_ = common::Qty{5000.},
      .max_loss_ = 1000.};
  common::TradeEngineCfg tempcfg = {
      .clip_ = common::Qty{100000},
      .threshold_ = 0.001,  // Very low threshold to trigger orders
      .risk_cfg_ = risk};
  cfg.emplace(INI_CONFIG.get("meta", "ticker"), tempcfg);

  // Setup Logger::Producer
  auto producer = logger->make_producer();

  // Setup ResponseManager and TradeEngine
  auto response_manager = std::make_unique<trading::ResponseManager>(
      producer, execution_report_pool.get(), order_cancel_reject_pool.get(),
      order_mass_cancel_report_pool.get());

  auto trade_engine = std::make_unique<TestTradeEngine>(
      producer, market_update_pool.get(), market_data_pool.get(),
      response_manager.get(), cfg);

  // Setup MockOrderGateway and connect to TradeEngine
  auto mock_gateway = std::make_unique<TestMockOrderGateway>(producer);
  mock_gateway->init_trade_engine(trade_engine.get());
  trade_engine->init_order_gateway_mock(mock_gateway.get());

  // Setup MdCore with separate pool for decoding
  common::MemoryPool<MarketData> decode_pool(65536);
  MdCore md_core(producer, &decode_pool);

  // Get OrderBook reference
  TestOrderBook order_book(INI_CONFIG.get("meta", "ticker"), producer);
  order_book.set_trade_engine(trade_engine.get());

  BenchmarkStats decode_stats, orderbook_stats, e2e_stats;

  size_t depth_count = 0;
  size_t trade_count = 0;
  size_t snapshot_count = 0;

  for (const auto& line : lines) {
    const auto e2e_start = common::rdtsc();

    // DECODE
    const auto decode_start = common::rdtsc();
    const WireMessage wire_msg = md_core.decode(line);
    decode_stats.record(common::rdtsc() - decode_start);

    // DISPATCH + CONVERT + ORDERBOOK + FEATURE + STRATEGY
    std::string_view msg_type;
    BinanceDispatchRouter::process_message<BinanceFuturesTraits>(wire_msg,
        [&msg_type](std::string_view type) { msg_type = type; });

    const auto orderbook_start = common::rdtsc();
    if (msg_type == "X") {
      // Market data update
      MarketUpdateData update_data = md_core.create_market_data_message(wire_msg);
      for (const auto* md : update_data.data) {
        if (md) {
          order_book.on_market_data_updated(md);
        }
      }
      if (line.find("@depth") != std::string::npos) {
        depth_count++;
      } else {
        trade_count++;
      }
    } else if (msg_type == "W") {
      // Snapshot
      MarketUpdateData update_data = md_core.create_snapshot_data_message(wire_msg);
      for (const auto* md : update_data.data) {
        if (md) {
          order_book.on_market_data_updated(md);
        }
      }
      snapshot_count++;
    }
    orderbook_stats.record(common::rdtsc() - orderbook_start);

    e2e_stats.record(common::rdtsc() - e2e_start);
  }

  // Stop trade engine before destruction
  trade_engine->stop();

  printf("\n=== Full Pipeline (Decode → OrderBook → Feature → Strategy → Encoder) ===\n");
  printf("Processed: %zu depth, %zu trade, %zu snapshot (total: %zu lines)\n",
      depth_count, trade_count, snapshot_count, lines.size());
  decode_stats.report("DECODE");
  orderbook_stats.report("OB+FE+STRAT");
  e2e_stats.report("E2E");

  // Print final BBO
  const auto* bbo = order_book.get_bbo();
  if (bbo) {
    printf("Final BBO: bid=%.4f (%.1f), ask=%.4f (%.1f)\n",
        bbo->bid_price.value, bbo->bid_qty.value,
        bbo->ask_price.value, bbo->ask_qty.value);
  }

  // Report order encoding stats from strategy
  mock_gateway->report();

  // Additional: Direct encoder benchmark (simulates orders being generated)
  printf("\n=== Direct Encoder Benchmark (simulated orders) ===\n");
  BenchmarkStats encode_stats;
  core::BinanceFuturesOeEncoder encoder(producer);

  // Simulate 1000 orders for encoder benchmark
  for (int i = 0; i < 1000; ++i) {
    trading::NewSingleOrderData order_data{
        .cl_order_id = common::OrderId{static_cast<uint64_t>(i + 1)},
        .symbol = INI_CONFIG.get("meta", "ticker"),
        .side = (i % 2 == 0) ? trading::OrderSide::kBuy : trading::OrderSide::kSell,
        .order_qty = common::Qty{100.0},
        .ord_type = trading::OrderType::kLimit,
        .price = common::Price{1.9230 + (i % 10) * 0.0001},
        .time_in_force = trading::TimeInForce::kGoodTillCancel,
        .self_trade_prevention_mode = trading::SelfTradePreventionMode::kExpireTaker,
        .position_side = common::PositionSide::kLong};

    const auto start = common::rdtsc();
    auto msg = encoder.create_order_message(order_data);
    encode_stats.record(common::rdtsc() - start);

    // Prevent optimization
    if (msg.empty()) {
      printf("Empty message!\n");
    }
  }
  encode_stats.report("ENCODE (new order)");
}

int main(int argc, char** argv) {
  std::string data_file = "data/benchmark/repository_1.txt";
  if (argc > 1) {
    data_file = argv[1];
  }

  // Load config for XRPUSDC
  INI_CONFIG.load("resources/config-xrpusdc.ini");

  printf("=== Full Pipeline Benchmark ===\n");
  printf("Data file: %s\n", data_file.c_str());

  auto lines = read_all_lines(data_file);
  if (lines.empty()) {
    fprintf(stderr, "No data loaded\n");
    return 1;
  }
  printf("Loaded %zu lines\n", lines.size());

  common::Logger logger;
  benchmark_full_pipeline(lines, &logger);

  printf("\n=== Benchmark Complete ===\n");
  return 0;
}
