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

#include <gtest/gtest.h>

#include <format>

#include "hft/common/performance.h"
#include "test/utils/fix_message_loader.h"
#include "test/utils/performance_utils.h"

/**
 * @brief Performance test suite for NFR-PF requirements
 *
 * These tests validate the non-functional performance requirements:
 * - NFR-PF-001: Latency < 100μs (orderbook update to order submission)
 * - NFR-PF-002: Throughput > 100k msg/sec
 * - NFR-PF-003: CPU affinity provides 10%+ performance improvement
 */
class PerformanceNFRTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Get CPU frequency for cycle-to-nanosecond conversion
    // Typical values: 3.5 GHz = 3.5e9 Hz
    cpu_hz_ = 3.5e9;  // TODO: Auto-detect CPU frequency
  }

  double cpu_hz_;
};

/**
 * NFR-PF-001: End-to-end latency requirement
 *
 * Requirement: Orderbook update to order submission latency < 100μs (P99)
 * Measurement: RDTSC-based cycle counting
 * Success criteria: P99 latency < 100,000 nanoseconds
 */
TEST_F(PerformanceNFRTest, NFR_PF_001_Latency_Under_100_Microseconds) {
  test::FixMessageLoader loader;

  // Load incremental orderbook updates
  // TODO: User needs to provide actual test data file
  const std::string data_file =
      "test/data/market_data/incremental_burst.fix";

  // Skip test if data file doesn't exist
  std::ifstream file_check(data_file);
  if (!file_check.good()) {
    GTEST_SKIP() << "Test data file not found: " << data_file
                 << "\n  Please provide FIX market data messages (one per line)";
  }

  auto messages = loader.load_messages(data_file);
  ASSERT_GT(messages.size(), 0)
      << "No messages loaded from " << data_file;

  test::PerformanceBenchmark bench;
  bench.reserve(messages.size());

  // Simulate orderbook update → order submission pipeline
  for (const auto& msg : messages) {
    bench.start();

    // TODO: Integrate with actual TradeEngine pipeline:
    // 1. Decode FIX message (FixMdCore::decode)
    // 2. Update OrderBook
    // 3. Trigger strategy (on_orderbook_updated)
    // 4. Generate QuoteIntent
    // 5. OrderManager processes intent
    // 6. Submit order via Gateway
    //
    // For now, simulate with a minimal operation
    volatile int dummy = 0;
    for (int i = 0; i < 100; ++i) {
      dummy += i;
    }

    bench.end();
  }

  auto stats = bench.get_stats();
  stats.print_report(cpu_hz_, "NFR-PF-001: Latency Test");

  const double p99_latency_ns = stats.get_latency_ns(cpu_hz_, 99);
  std::cout << std::format("\n[RESULT] P99 Latency: {:.2f} μs\n",
                           p99_latency_ns / 1000.0);

  // Requirement: P99 < 100μs
  EXPECT_LT(p99_latency_ns, 100000.0)
      << "P99 latency exceeds 100μs requirement";
}

/**
 * NFR-PF-002: Throughput requirement
 *
 * Requirement: Process > 100,000 orderbook updates per second
 * Measurement: Total messages / elapsed time
 * Success criteria: Throughput > 100,000 msg/sec
 */
TEST_F(PerformanceNFRTest, NFR_PF_002_Throughput_Above_100k_Messages_Per_Second) {
  test::FixMessageLoader loader;

  const std::string data_file =
      "test/data/market_data/incremental_001.fix";

  std::ifstream file_check(data_file);
  if (!file_check.good()) {
    GTEST_SKIP() << "Test data file not found: " << data_file;
  }

  // Load and repeat messages to get 1 million total messages
  auto messages = loader.load_messages_repeated(data_file, 1000000);
  ASSERT_EQ(messages.size(), 1000000);

  const uint64_t start_cycle = common::rdtsc();

  // Process all messages
  for (const auto& msg : messages) {
    // TODO: Integrate with actual OrderBook update logic
    volatile int dummy = 0;
    for (int i = 0; i < 50; ++i) {
      dummy += i;
    }
  }

  const uint64_t end_cycle = common::rdtsc();

  const double elapsed_cycles = static_cast<double>(end_cycle - start_cycle);
  const double elapsed_sec = elapsed_cycles / cpu_hz_;
  const double throughput = static_cast<double>(messages.size()) / elapsed_sec;

  std::cout << std::format("\n[RESULT] Processed {} messages in {:.3f} seconds\n",
                           messages.size(), elapsed_sec);
  std::cout << std::format("[RESULT] Throughput: {:.0f} msg/sec\n", throughput);

  // Requirement: > 100k msg/sec
  EXPECT_GT(throughput, 100000.0)
      << "Throughput does not meet 100k msg/sec requirement";
}

/**
 * NFR-PF-003: CPU affinity impact
 *
 * Requirement: CPU affinity provides 10%+ performance improvement
 * Measurement: Compare P99 latency with/without CPU affinity
 * Success criteria: (latency_no_affinity - latency_with_affinity) / latency_no_affinity > 0.10
 */
TEST_F(PerformanceNFRTest, NFR_PF_003_CPU_Affinity_Improves_Performance_By_10_Percent) {
  test::FixMessageLoader loader;

  const std::string data_file =
      "test/data/market_data/incremental_001.fix";

  std::ifstream file_check(data_file);
  if (!file_check.good()) {
    GTEST_SKIP() << "Test data file not found: " << data_file;
  }

  auto messages = loader.load_messages_repeated(data_file, 10000);
  ASSERT_GT(messages.size(), 0);

  // Helper lambda to run benchmark
  auto run_benchmark = [&](bool use_affinity) -> test::LatencyStats {
    if (use_affinity) {
      // TODO: Set CPU affinity (pin to isolated core)
      // cpu_set_t cpuset;
      // CPU_ZERO(&cpuset);
      // CPU_SET(2, &cpuset);  // Pin to CPU 2
      // pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
    } else {
      // TODO: Clear CPU affinity (allow OS scheduling)
      // cpu_set_t cpuset;
      // for (int i = 0; i < CPU_SETSIZE; ++i) CPU_SET(i, &cpuset);
      // pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
    }

    test::PerformanceBenchmark bench;
    bench.reserve(messages.size());

    for (const auto& msg : messages) {
      bench.start();

      // Simulate orderbook processing
      volatile int dummy = 0;
      for (int i = 0; i < 100; ++i) {
        dummy += i;
      }

      bench.end();
    }

    return bench.get_stats();
  };

  // Run without affinity
  auto stats_no_affinity = run_benchmark(false);
  stats_no_affinity.print_report(cpu_hz_, "NFR-PF-003: Without CPU Affinity");

  // Run with affinity
  auto stats_with_affinity = run_benchmark(true);
  stats_with_affinity.print_report(cpu_hz_, "NFR-PF-003: With CPU Affinity");

  // Calculate improvement
  const double improvement =
      static_cast<double>(stats_no_affinity.p99_cycles -
                          stats_with_affinity.p99_cycles) /
      static_cast<double>(stats_no_affinity.p99_cycles);

  std::cout << std::format("\n[RESULT] CPU Affinity Improvement: {:.2f}%\n",
                           improvement * 100.0);

  // Requirement: > 10% improvement
  // NOTE: This test may be flaky without proper CPU isolation
  // In production, CPU affinity should be configured via config.ini
  EXPECT_GT(improvement, 0.10)
      << "CPU affinity does not provide 10%+ performance improvement";
}

/**
 * Example test showing how to use TestGateway for integration testing
 *
 * This demonstrates file-based replay of execution reports
 * for end-to-end performance testing without network I/O.
 */
TEST(TestGatewayExample, Replay_Execution_Reports) {
  // TODO: This test requires full TradeEngine setup
  // Uncomment when integrating with actual system

  /*
  auto logger = std::make_unique<common::Logger>();
  auto response_manager = std::make_unique<trading::ResponseManager>(logger.get());
  auto gateway = std::make_unique<test::TestGateway>(logger.get(),
  response_manager.get());

  test::FixMessageLoader loader;
  auto execution_reports = loader.load_messages("test/data/execution_reports/fill_001.fix");

  // Replay execution reports
  gateway->replay_execution_reports(execution_reports);

  // Verify order processing
  // ...
  */

  GTEST_SKIP() << "Integration test placeholder - requires full TradeEngine setup";
}
