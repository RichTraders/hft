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

#include "common/fixed_point.hpp"
#include "common/ini_config.hpp"
#include "common/logger.h"
#include "common/memory_pool.hpp"
#include "common/performance.h"
#include "core/market_data.h"
#include "core/websocket/market_data/exchanges/binance/futures/binance_futures_traits.h"
#include "core/websocket/market_data/json_md_decoder.hpp"
#include "core/websocket/market_data/ws_md_core.h"
#include "core/websocket/market_data/ws_md_domain_mapper.h"

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

  void clear() { samples.clear(); }
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

// ============================================================================
// Baseline (double) Benchmark
// ============================================================================

void benchmark_baseline_double(const std::vector<std::string>& lines,
    const common::Logger::Producer& logger,
    common::MemoryPool<MarketData>* pool) {
  using MdCore = core::WsMdCore<BinanceFuturesTraits, core::JsonMdDecoder>;
  using WireMessage = MdCore::WireMessage;

  MdCore md_core(logger, pool);

  BenchmarkStats decode_stats, dispatch_stats, convert_stats, e2e_stats;

  size_t depth_count = 0;
  size_t trade_count = 0;
  size_t snapshot_count = 0;

  for (const auto& line : lines) {
    const auto e2e_start = common::rdtsc();

    // DECODE
    const auto decode_start = common::rdtsc();
    const WireMessage wire_msg = md_core.decode(line);
    decode_stats.record(common::rdtsc() - decode_start);

    // DISPATCH
    std::string_view msg_type;
    const auto dispatch_start = common::rdtsc();
    BinanceDispatchRouter::process_message<BinanceFuturesTraits>(wire_msg,
        [&msg_type](std::string_view type) { msg_type = type; });
    dispatch_stats.record(common::rdtsc() - dispatch_start);

    // CONVERT
    const auto convert_start = common::rdtsc();
    if (msg_type == "X") {
      auto result = md_core.create_market_data_message(wire_msg);
      (void)result;
      if (line.find("@depth") != std::string::npos) {
        depth_count++;
      } else {
        trade_count++;
      }
    } else if (msg_type == "W") {
      auto result = md_core.create_snapshot_data_message(wire_msg);
      (void)result;
      snapshot_count++;
    }
    convert_stats.record(common::rdtsc() - convert_start);

    e2e_stats.record(common::rdtsc() - e2e_start);
  }

  printf("\n=== Baseline (double) ===\n");
  printf("Processed: %zu depth, %zu trade, %zu snapshot (total: %zu lines)\n",
      depth_count, trade_count, snapshot_count, lines.size());
  decode_stats.report("DECODE");
  dispatch_stats.report("DISPATCH");
  convert_stats.report("CONVERT");
  e2e_stats.report("E2E");
}

// ============================================================================
// FixedPoint via Double (conversion from double)
// ============================================================================

void benchmark_fixedpoint_via_double(const std::vector<std::string>& lines,
    const common::Logger::Producer& logger,
    common::MemoryPool<MarketData>* pool) {
  using MdCore = core::WsMdCore<BinanceFuturesTraits, core::JsonMdDecoder>;
  using WireMessage = MdCore::WireMessage;
  using FixedPrice = common::FixedPoint<int64_t, 100000000>;
  using FixedQty = common::FixedPoint<int64_t, 100000000>;

  MdCore md_core(logger, pool);

  BenchmarkStats decode_stats, dispatch_stats, convert_stats, e2e_stats;

  size_t depth_count = 0;
  size_t trade_count = 0;
  size_t snapshot_count = 0;

  for (const auto& line : lines) {
    const auto e2e_start = common::rdtsc();

    // DECODE (same as baseline - glaze → double)
    const auto decode_start = common::rdtsc();
    const WireMessage wire_msg = md_core.decode(line);
    decode_stats.record(common::rdtsc() - decode_start);

    // DISPATCH
    std::string_view msg_type;
    const auto dispatch_start = common::rdtsc();
    BinanceDispatchRouter::process_message<BinanceFuturesTraits>(wire_msg,
        [&msg_type](std::string_view type) { msg_type = type; });
    dispatch_stats.record(common::rdtsc() - dispatch_start);

    // CONVERT (with additional double → FixedPoint conversion)
    const auto convert_start = common::rdtsc();
    if (msg_type == "X") {
      // Visit depth or trade and convert to FixedPoint
      std::visit(
          [&](const auto& msg) {
            using T = std::decay_t<decltype(msg)>;
            if constexpr (std::is_same_v<T, schema::futures::DepthResponse>) {
              depth_count++;
              for (const auto& bid : msg.data.bids) {
                FixedPrice price(bid[0]);  // double → FixedPoint
                FixedQty qty(bid[1]);
                (void)price;
                (void)qty;
              }
              for (const auto& ask : msg.data.asks) {
                FixedPrice price(ask[0]);
                FixedQty qty(ask[1]);
                (void)price;
                (void)qty;
              }
            } else if constexpr (std::is_same_v<T, schema::futures::TradeEvent>) {
              trade_count++;
              FixedPrice price(msg.data.price);
              FixedQty qty(msg.data.quantity);
              (void)price;
              (void)qty;
            }
          },
          wire_msg);
    } else if (msg_type == "W") {
      std::visit(
          [&](const auto& msg) {
            using T = std::decay_t<decltype(msg)>;
            if constexpr (std::is_same_v<T, schema::futures::DepthSnapshot>) {
              snapshot_count++;
              for (const auto& bid : msg.result.bids) {
                FixedPrice price(bid[0]);
                FixedQty qty(bid[1]);
                (void)price;
                (void)qty;
              }
              for (const auto& ask : msg.result.asks) {
                FixedPrice price(ask[0]);
                FixedQty qty(ask[1]);
                (void)price;
                (void)qty;
              }
            }
          },
          wire_msg);
    }
    convert_stats.record(common::rdtsc() - convert_start);

    e2e_stats.record(common::rdtsc() - e2e_start);
  }

  printf("\n=== FixedPoint (via double) ===\n");
  printf("Processed: %zu depth, %zu trade, %zu snapshot (total: %zu lines)\n",
      depth_count, trade_count, snapshot_count, lines.size());
  decode_stats.report("DECODE");
  dispatch_stats.report("DISPATCH");
  convert_stats.report("CONVERT");
  e2e_stats.report("E2E");
}

// ============================================================================
// FixedPoint Direct String Parsing Benchmark
// ============================================================================

// Direct string extraction from JSON (simplified for benchmarking)
// This simulates what a custom glaze parser would do
void benchmark_fixedpoint_direct(const std::vector<std::string>& lines,
    const common::Logger::Producer& /*logger*/,
    common::MemoryPool<MarketData>* /*pool*/) {
  using FixedPrice = common::FixedPoint<int64_t, 100000000>;
  using FixedQty = common::FixedPoint<int64_t, 100000000>;

  BenchmarkStats parse_stats;

  size_t depth_count = 0;
  size_t trade_count = 0;
  size_t snapshot_count = 0;

  // Pre-extract price/qty strings for direct parsing benchmark
  // This measures pure string→FixedPoint conversion overhead
  std::vector<std::pair<std::string, std::string>> price_qty_pairs;

  for (const auto& line : lines) {
    if (line.find("@depth") != std::string::npos ||
        line.find("snapshot") != std::string::npos) {
      // Extract bid/ask price-qty pairs from JSON
      size_t pos = 0;
      while ((pos = line.find("[\"", pos)) != std::string::npos) {
        size_t price_start = pos + 2;
        size_t price_end = line.find("\"", price_start);
        if (price_end == std::string::npos) break;

        size_t qty_start = line.find("\"", price_end + 1);
        if (qty_start == std::string::npos) break;
        qty_start++;
        size_t qty_end = line.find("\"", qty_start);
        if (qty_end == std::string::npos) break;

        price_qty_pairs.emplace_back(
            line.substr(price_start, price_end - price_start),
            line.substr(qty_start, qty_end - qty_start));

        pos = qty_end + 1;
      }
      if (line.find("@depth") != std::string::npos) {
        depth_count++;
      } else {
        snapshot_count++;
      }
    } else if (line.find("@aggTrade") != std::string::npos) {
      // Extract price and quantity from trade
      auto find_quoted_value = [&line](const std::string& key) -> std::string {
        size_t key_pos = line.find("\"" + key + "\":\"");
        if (key_pos == std::string::npos) return "";
        size_t val_start = key_pos + key.length() + 4;
        size_t val_end = line.find("\"", val_start);
        if (val_end == std::string::npos) return "";
        return line.substr(val_start, val_end - val_start);
      };

      std::string price = find_quoted_value("p");
      std::string qty = find_quoted_value("q");
      if (!price.empty() && !qty.empty()) {
        price_qty_pairs.emplace_back(price, qty);
      }
      trade_count++;
    }
  }

  printf("\n=== FixedPoint (direct string parsing) ===\n");
  printf("Extracted %zu price-qty pairs from %zu depth, %zu trade, %zu snapshot\n",
      price_qty_pairs.size(), depth_count, trade_count, snapshot_count);

  // Now benchmark direct string → FixedPoint conversion
  const auto parse_start = common::rdtsc();
  for (const auto& [price_str, qty_str] : price_qty_pairs) {
    FixedPrice price = FixedPrice::from_string(price_str.c_str(), price_str.size());
    FixedQty qty = FixedQty::from_string(qty_str.c_str(), qty_str.size());
    (void)price;
    (void)qty;
  }
  const auto parse_end = common::rdtsc();

  uint64_t total_cycles = parse_end - parse_start;
  uint64_t avg_cycles = price_qty_pairs.empty() ? 0 : total_cycles / price_qty_pairs.size();

  printf("Total cycles: %llu, Avg per pair: %llu\n",
      static_cast<unsigned long long>(total_cycles),
      static_cast<unsigned long long>(avg_cycles));

  // Compare with double parsing
  const auto double_start = common::rdtsc();
  for (const auto& [price_str, qty_str] : price_qty_pairs) {
    double price = std::strtod(price_str.c_str(), nullptr);
    double qty = std::strtod(qty_str.c_str(), nullptr);
    (void)price;
    (void)qty;
  }
  const auto double_end = common::rdtsc();

  uint64_t double_total = double_end - double_start;
  uint64_t double_avg = price_qty_pairs.empty() ? 0 : double_total / price_qty_pairs.size();

  printf("Double parse - Total cycles: %llu, Avg per pair: %llu\n",
      static_cast<unsigned long long>(double_total),
      static_cast<unsigned long long>(double_avg));

  printf("Speedup: %.2fx\n",
      double_avg > 0 ? static_cast<double>(double_avg) / static_cast<double>(avg_cycles) : 0.0);
}

// ============================================================================
// Arithmetic Benchmark
// ============================================================================

void benchmark_arithmetic(const std::vector<std::string>& lines) {
  using FixedPrice = common::FixedPoint<int64_t, 100000000>;
  using FixedQty = common::FixedPoint<int64_t, 100000000>;

  // Extract some price/qty values for arithmetic benchmark
  std::vector<std::pair<double, double>> double_pairs;
  std::vector<std::pair<FixedPrice, FixedQty>> fixed_pairs;

  for (const auto& line : lines) {
    if (line.find("@aggTrade") != std::string::npos) {
      auto find_quoted_value = [&line](const std::string& key) -> std::string {
        size_t key_pos = line.find("\"" + key + "\":\"");
        if (key_pos == std::string::npos) return "";
        size_t val_start = key_pos + key.length() + 4;
        size_t val_end = line.find("\"", val_start);
        if (val_end == std::string::npos) return "";
        return line.substr(val_start, val_end - val_start);
      };

      std::string price_str = find_quoted_value("p");
      std::string qty_str = find_quoted_value("q");
      if (!price_str.empty() && !qty_str.empty()) {
        double price = std::strtod(price_str.c_str(), nullptr);
        double qty = std::strtod(qty_str.c_str(), nullptr);
        double_pairs.emplace_back(price, qty);
        fixed_pairs.emplace_back(
            FixedPrice::from_string(price_str.c_str(), price_str.size()),
            FixedQty::from_string(qty_str.c_str(), qty_str.size()));
      }
    }
    if (double_pairs.size() >= 10000) break;  // Enough samples
  }

  printf("\n=== Arithmetic Benchmark (price * qty) ===\n");
  printf("Samples: %zu pairs\n", double_pairs.size());

  // Double arithmetic
  volatile double double_sum = 0;
  const auto double_start = common::rdtsc();
  for (const auto& [price, qty] : double_pairs) {
    double_sum += price * qty;
  }
  const auto double_end = common::rdtsc();

  // FixedPoint arithmetic
  volatile int64_t fixed_sum = 0;
  const auto fixed_start = common::rdtsc();
  for (const auto& [price, qty] : fixed_pairs) {
    auto result = price * qty;
    fixed_sum += result.raw_value;
  }
  const auto fixed_end = common::rdtsc();

  uint64_t double_cycles = double_end - double_start;
  uint64_t fixed_cycles = fixed_end - fixed_start;

  printf("Double:     %llu cycles (avg: %llu per op)\n",
      static_cast<unsigned long long>(double_cycles),
      static_cast<unsigned long long>(double_pairs.empty() ? 0 : double_cycles / double_pairs.size()));
  printf("FixedPoint: %llu cycles (avg: %llu per op)\n",
      static_cast<unsigned long long>(fixed_cycles),
      static_cast<unsigned long long>(fixed_pairs.empty() ? 0 : fixed_cycles / fixed_pairs.size()));
  printf("Ratio: %.2fx\n",
      fixed_cycles > 0 ? static_cast<double>(double_cycles) / static_cast<double>(fixed_cycles) : 0.0);
}

int main(int argc, char** argv) {
  std::string data_file = "data/benchmark/repository_1.txt";
  if (argc > 1) {
    data_file = argv[1];
  }

  // Load config for XRPUSDC
  INI_CONFIG.load("resources/config-xrpusdc.ini");

  printf("=== FixedPoint vs Double Benchmark ===\n");
  printf("Data file: %s\n", data_file.c_str());

  auto lines = read_all_lines(data_file);
  if (lines.empty()) {
    fprintf(stderr, "No data loaded\n");
    return 1;
  }
  printf("Loaded %zu lines\n", lines.size());

  // Setup
  common::Logger logger;
  auto producer = logger.make_producer();
  common::MemoryPool<MarketData> pool(65536);

  // Run benchmarks
  benchmark_baseline_double(lines, producer, &pool);
  benchmark_fixedpoint_via_double(lines, producer, &pool);
  benchmark_fixedpoint_direct(lines, producer, &pool);
  benchmark_arithmetic(lines);

  printf("\n=== Benchmark Complete ===\n");
  return 0;
}
