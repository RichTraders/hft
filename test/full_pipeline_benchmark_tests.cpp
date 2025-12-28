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

#include <algorithm>
#include <chrono>
#include <fstream>
#include <numeric>
#include <vector>

#include "common/cpumanager/cpu_manager.h"
#include "common/ini_config.hpp"
#include "common/logger.h"
#include "common/performance.h"
#include "core/market_data.h"
#include "core/response_manager.h"
#include "order_gateway.hpp"
#include "strategy_config.hpp"
#include "trade_engine.hpp"

#ifdef USE_RING_BUFFER
// =============================================================================
// RingBuffer 기반 Full Pipeline Benchmark (MarketConsumer 사용)
// =============================================================================
#include "common/market_data_ring_buffer.hpp"
#include "core/transport/file_transport.h"
#include "core/websocket/market_data/ws_md_app.hpp"
#include "core/websocket/order_entry/ws_oe_app.hpp"
#include "market_consumer.hpp"

using namespace common;
using namespace core;
using namespace trading;

using FileMdStreamTransport = FileTransport<"MDStream">;
using FileMdApiTransport = FileTransport<"MDApi">;
using TestMdApp = WsMarketDataAppT<FileMdStreamTransport, FileMdApiTransport>;

using FileOeApiTransport = FileTransport<"OEApi">;
using FileOeStreamTransport = FileTransport<"OEStream">;
using TestOeApp = WsOrderEntryAppT<FileOeApiTransport, FileOeStreamTransport>;

using TestMarketConsumer = MarketConsumer<SelectedStrategy, TestMdApp>;
using TestOrderGateway = OrderGateway<TestOeApp>;
using TestTradeEngine = TradeEngine<SelectedStrategy>;

class RingBufferPipelineBenchmark : public ::testing::Test {
 protected:
  static inline std::unique_ptr<Logger> logger_;
  static inline std::unique_ptr<Logger::Producer> producer_;
  static inline std::unique_ptr<CpuManager> cpu_manager_;

  static std::string make_log_filename() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now{};
    localtime_r(&time_t_now, &tm_now);
    char buf[64];
    std::strftime(buf,
        sizeof(buf),
        "benchmark_ringbuffer_%Y%m%d%H%M%S.log",
        &tm_now);
    return buf;
  }

  static void SetUpTestSuite() {
    INI_CONFIG.load("resources/config-xrpusdc.ini");
    PRECISION_CONFIG.initialize();
    logger_ = std::make_unique<Logger>();
    logger_->setLevel(LogLevel::kInfo);
    logger_->clearSink();
    auto log_filename = make_log_filename();
    std::cout << "Log file: " << log_filename << std::endl;
    logger_->addSink(
        std::make_unique<FileSink>(log_filename, 100 * 1024 * 1024));
    producer_ = std::make_unique<Logger::Producer>(logger_->make_producer());
  }

  static void TearDownTestSuite() {
    cpu_manager_.reset();
    producer_.reset();
    logger_->shutdown();
    logger_.reset();
  }

  void SetUp() override {
    // RingBuffer 생성
    ring_buffer_ = std::make_unique<MarketDataRingBuffer>();

    // Response 관련 Pool
    execution_report_pool_ =
        std::make_unique<MemoryPool<ExecutionReport>>(1024);
    order_cancel_reject_pool_ =
        std::make_unique<MemoryPool<OrderCancelReject>>(1024);
    order_mass_cancel_report_pool_ =
        std::make_unique<MemoryPool<OrderMassCancelReport>>(1024);

    TradeEngineCfgHashMap config_map;
    config_map[INI_CONFIG.get("meta", "ticker")] = {.clip_ = Qty{0},
        .threshold_ = 0,
        .risk_cfg_ =
            RiskCfg(Qty{INI_CONFIG.get_double("risk", "max_order_size")},
                Qty{INI_CONFIG.get_double("risk", "max_position")},
                Qty{INI_CONFIG.get_double("risk", "min_position", 0.)},
                INI_CONFIG.get_double("risk", "max_loss"))};

    response_manager_ = std::make_unique<ResponseManager>(*producer_,
        execution_report_pool_.get(),
        order_cancel_reject_pool_.get(),
        order_mass_cancel_report_pool_.get());

    order_gateway_ =
        std::make_unique<TestOrderGateway>(*producer_, response_manager_.get());

    // RingBuffer 기반 TradeEngine
    trade_engine_ = std::make_unique<TestTradeEngine>(*producer_,
        ring_buffer_.get(),
        response_manager_.get(),
        config_map);

    trade_engine_->init_order_gateway(order_gateway_.get());
    order_gateway_->init_trade_engine(trade_engine_.get());

    order_gateway_->app().api_transport().simulate_connect();
    order_gateway_->app().api_transport().enable_order_simulator(
        std::chrono::milliseconds{1}, true);

    market_consumer_ = std::make_unique<TestMarketConsumer>(*producer_,
        trade_engine_.get(),
        ring_buffer_.get());

    market_consumer_->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    cpu_manager_ = std::make_unique<CpuManager>(*producer_);
    std::string cpu_init_result;
    if (cpu_manager_->init_cpu_group(cpu_init_result)) {
      std::cout << "CPU group init: " << cpu_init_result << std::endl;
    }
    if (cpu_manager_->init_cpu_to_tid()) {
      std::cout << "CPU to TID init skipped" << std::endl;
    }
  }

  void TearDown() override {
    order_gateway_->app().api_transport().stop_simulator();
    market_consumer_.reset();
    trade_engine_->stop();
    order_gateway_->stop();
    trade_engine_.reset();
    order_gateway_.reset();
    response_manager_.reset();
    ring_buffer_.reset();

    order_mass_cancel_report_pool_.reset();
    order_cancel_reject_pool_.reset();
    execution_report_pool_.reset();
  }

  void inject_stream(const std::string& json) {
    market_consumer_->app().stream_transport().inject_message(json);
  }

  void inject_snapshot(const std::string& json) {
    market_consumer_->app().api_transport().inject_message(json);
  }

  void simulate_connection() {
    market_consumer_->app().api_transport().inject_message("__CONNECTED__");
  }

  struct BenchmarkData {
    std::string snapshot;
    std::vector<std::string> stream_messages;
  };

  static bool is_valid_json(const std::string& line) {
    if (line.empty())
      return false;
    size_t start = line.find_first_not_of(" \t\r\n");
    size_t end = line.find_last_not_of(" \t\r\n");
    if (start == std::string::npos || end == std::string::npos)
      return false;
    return line[start] == '{' && line[end] == '}';
  }

  static BenchmarkData load_benchmark_file(const std::string& filepath) {
    BenchmarkData data;
    std::ifstream file(filepath);
    if (!file.is_open()) {
      throw std::runtime_error("Cannot open benchmark file: " + filepath);
    }

    size_t skipped = 0;
    std::string line;
    while (std::getline(file, line)) {
      if (!is_valid_json(line)) {
        ++skipped;
        continue;
      }

      if (data.snapshot.empty() &&
          line.find("\"id\":\"snapshot_") != std::string::npos) {
        data.snapshot = std::move(line);
      } else {
        data.stream_messages.push_back(std::move(line));
      }
    }
    if (skipped > 0) {
      std::cout << "Skipped " << skipped << " non-JSON lines" << std::endl;
    }
    return data;
  }

  std::unique_ptr<MarketDataRingBuffer> ring_buffer_;
  std::unique_ptr<MemoryPool<ExecutionReport>> execution_report_pool_;
  std::unique_ptr<MemoryPool<OrderCancelReject>> order_cancel_reject_pool_;
  std::unique_ptr<MemoryPool<OrderMassCancelReport>>
      order_mass_cancel_report_pool_;
  std::unique_ptr<ResponseManager> response_manager_;
  std::unique_ptr<TestOrderGateway> order_gateway_;
  std::unique_ptr<TestTradeEngine> trade_engine_;
  std::unique_ptr<TestMarketConsumer> market_consumer_;
};

TEST_F(RingBufferPipelineBenchmark, RealtimeReplay) {
  const std::string data_file = "data/benchmark/test_file.txt";
  auto benchmark_data = load_benchmark_file(data_file);

  std::cout << "\n=== RingBuffer Pipeline Benchmark (via MarketConsumer) ===" << std::endl;
  std::cout << "Data file: " << data_file << std::endl;
  std::cout << "Snapshot: " << (benchmark_data.snapshot.empty() ? "NO" : "YES")
            << std::endl;
  std::cout << "Stream messages: " << benchmark_data.stream_messages.size()
            << std::endl;

  simulate_connection();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  if (!benchmark_data.snapshot.empty()) {
    inject_snapshot(benchmark_data.snapshot);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  // Replay stream messages with realtime timing
  constexpr double kSpeedMultiplier = 100.0;  // 100x speed
  uint64_t last_timestamp = 0;
  size_t msg_count = 0;

  auto start_time = std::chrono::high_resolution_clock::now();

  for (const auto& msg : benchmark_data.stream_messages) {
    // Extract timestamp from message (E field)
    uint64_t timestamp = 0;
    auto e_pos = msg.find("\"E\":");
    if (e_pos != std::string::npos) {
      e_pos += 4;
      while (e_pos < msg.size() && msg[e_pos] >= '0' && msg[e_pos] <= '9') {
        timestamp = timestamp * 10 + (msg[e_pos] - '0');
        ++e_pos;
      }
    }

    // Apply timing delay
    if (last_timestamp > 0 && timestamp > last_timestamp) {
      uint64_t delta_ms = timestamp - last_timestamp;
      auto delay = std::chrono::microseconds(
          static_cast<uint64_t>(delta_ms * 1000 / kSpeedMultiplier));
      std::this_thread::sleep_for(delay);
    }
    last_timestamp = timestamp;

    inject_stream(msg);
    ++msg_count;
  }

  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time)
                         .count();

  std::cout << "Messages replayed: " << msg_count << std::endl;

  // Wait for worker thread to finish processing
  std::this_thread::sleep_for(std::chrono::seconds(3));

  std::cout << "\n--- RingBuffer Replay Complete ---" << std::endl;
  std::cout << "Total replay time: " << duration_ms << " ms" << std::endl;
  std::cout << "Check benchmark_ringbuffer.log for RDTSC measurements" << std::endl;

  EXPECT_GT(duration_ms, 0);
}

#else
// =============================================================================
// Pool/Queue 기반 Full Pipeline Benchmark (기존 방식)
// =============================================================================
#include "common/memory_pool.hpp"
#include "core/transport/file_transport.h"
#include "core/websocket/market_data/ws_md_app.hpp"
#include "core/websocket/order_entry/ws_oe_app.hpp"
#include "market_consumer.hpp"

using namespace common;
using namespace core;
using namespace trading;

using FileMdStreamTransport = FileTransport<"MDStream">;
using FileMdApiTransport = FileTransport<"MDApi">;
using TestMdApp = WsMarketDataAppT<FileMdStreamTransport, FileMdApiTransport>;

using FileOeApiTransport = FileTransport<"OEApi">;
using FileOeStreamTransport = FileTransport<"OEStream">;
using TestOeApp = WsOrderEntryAppT<FileOeApiTransport, FileOeStreamTransport>;

using TestMarketConsumer = MarketConsumer<SelectedStrategy, TestMdApp>;
using TestOrderGateway = OrderGateway<TestOeApp>;
using TestTradeEngine = TradeEngine<SelectedStrategy>;

class FullPipelineBenchmark : public ::testing::Test {
 protected:
  static inline std::unique_ptr<Logger> logger_;
  static inline std::unique_ptr<Logger::Producer> producer_;
  static inline std::unique_ptr<CpuManager> cpu_manager_;

  static std::string make_log_filename() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now{};
    localtime_r(&time_t_now, &tm_now);
    char buf[64];
    std::strftime(buf,
        sizeof(buf),
#ifdef USE_ONEPASS_DECODER
        "benchmark_onepass_%Y%m%d%H%M%S.log",
#else
        "benchmark_json_%Y%m%d%H%M%S.log",
#endif
        &tm_now);
    return buf;
  }

  static void SetUpTestSuite() {
    INI_CONFIG.load("resources/config-xrpusdc.ini");
    PRECISION_CONFIG.initialize();

    logger_ = std::make_unique<Logger>();
    logger_->setLevel(LogLevel::kInfo);
    logger_->clearSink();
    auto log_filename = make_log_filename();
    std::cout << "Log file: " << log_filename << std::endl;
    logger_->addSink(
        std::make_unique<FileSink>(log_filename, 100 * 1024 * 1024));
    producer_ = std::make_unique<Logger::Producer>(logger_->make_producer());
  }

  static void TearDownTestSuite() {
    cpu_manager_.reset();
    producer_.reset();
    logger_->shutdown();
    logger_.reset();
  }

  void SetUp() override {
    market_update_data_pool_ =
        std::make_unique<MemoryPool<MarketUpdateData>>(65536);
    market_data_pool_ = std::make_unique<MemoryPool<MarketData>>(65536);

    execution_report_pool_ =
        std::make_unique<MemoryPool<ExecutionReport>>(1024);
    order_cancel_reject_pool_ =
        std::make_unique<MemoryPool<OrderCancelReject>>(1024);
    order_mass_cancel_report_pool_ =
        std::make_unique<MemoryPool<OrderMassCancelReport>>(1024);

    TradeEngineCfgHashMap config_map;
    config_map[INI_CONFIG.get("meta", "ticker")] = {.clip_ = Qty{0},
        .threshold_ = 0,
        .risk_cfg_ =
            RiskCfg(Qty{INI_CONFIG.get_double("risk", "max_order_size")},
                Qty{INI_CONFIG.get_double("risk", "max_position")},
                Qty{INI_CONFIG.get_double("risk", "min_position", 0.)},
                INI_CONFIG.get_double("risk", "max_loss"))};

    response_manager_ = std::make_unique<ResponseManager>(*producer_,
        execution_report_pool_.get(),
        order_cancel_reject_pool_.get(),
        order_mass_cancel_report_pool_.get());

    order_gateway_ =
        std::make_unique<TestOrderGateway>(*producer_, response_manager_.get());

    trade_engine_ = std::make_unique<TestTradeEngine>(*producer_,
        market_update_data_pool_.get(),
        market_data_pool_.get(),
        response_manager_.get(),
        config_map);

    trade_engine_->init_order_gateway(order_gateway_.get());
    order_gateway_->init_trade_engine(trade_engine_.get());

    order_gateway_->app().api_transport().simulate_connect();
    order_gateway_->app().api_transport().enable_order_simulator(
        std::chrono::milliseconds{1}, true);

    market_consumer_ = std::make_unique<TestMarketConsumer>(*producer_,
        trade_engine_.get(),
        market_update_data_pool_.get(),
        market_data_pool_.get());

    market_consumer_->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    cpu_manager_ = std::make_unique<CpuManager>(*producer_);
    std::string cpu_init_result;
    if (cpu_manager_->init_cpu_group(cpu_init_result)) {
      std::cout << "CPU group init: " << cpu_init_result << std::endl;
    }
    if (cpu_manager_->init_cpu_to_tid()) {
      std::cout << "CPU to TID init skipped" << std::endl;
    }
  }

  void TearDown() override {
    order_gateway_->app().api_transport().stop_simulator();
    market_consumer_.reset();
    trade_engine_->stop();
    order_gateway_->stop();
    trade_engine_.reset();
    order_gateway_.reset();
    response_manager_.reset();

    order_mass_cancel_report_pool_.reset();
    order_cancel_reject_pool_.reset();
    execution_report_pool_.reset();
    market_data_pool_.reset();
    market_update_data_pool_.reset();
  }

  void inject_stream(const std::string& json) {
    market_consumer_->app().stream_transport().inject_message(json);
  }

  void inject_snapshot(const std::string& json) {
    market_consumer_->app().api_transport().inject_message(json);
  }

  void simulate_connection() {
    // Simulate API transport connection -> triggers login flow -> kBuffering
    market_consumer_->app().api_transport().inject_message("__CONNECTED__");
  }

  struct BenchmarkData {
    std::string snapshot;
    std::vector<std::string> stream_messages;
  };

  static bool is_valid_json(const std::string& line) {
    if (line.empty())
      return false;
    // JSON object must start with '{' and end with '}'
    size_t start = line.find_first_not_of(" \t\r\n");
    size_t end = line.find_last_not_of(" \t\r\n");
    if (start == std::string::npos || end == std::string::npos)
      return false;
    return line[start] == '{' && line[end] == '}';
  }

  static BenchmarkData load_benchmark_file(const std::string& filepath) {
    BenchmarkData data;
    std::ifstream file(filepath);
    if (!file.is_open()) {
      throw std::runtime_error("Cannot open benchmark file: " + filepath);
    }

    size_t skipped = 0;
    std::string line;
    while (std::getline(file, line)) {
      if (!is_valid_json(line)) {
        ++skipped;
        continue;
      }

      if (data.snapshot.empty() &&
          line.find("\"id\":\"snapshot_") != std::string::npos) {
        data.snapshot = std::move(line);
      } else {
        data.stream_messages.push_back(std::move(line));
      }
    }
    if (skipped > 0) {
      std::cout << "Skipped " << skipped << " non-JSON lines" << std::endl;
    }
    return data;
  }

  std::unique_ptr<MemoryPool<MarketUpdateData>> market_update_data_pool_;
  std::unique_ptr<MemoryPool<MarketData>> market_data_pool_;
  std::unique_ptr<MemoryPool<ExecutionReport>> execution_report_pool_;
  std::unique_ptr<MemoryPool<OrderCancelReject>> order_cancel_reject_pool_;
  std::unique_ptr<MemoryPool<OrderMassCancelReport>>
      order_mass_cancel_report_pool_;
  std::unique_ptr<ResponseManager> response_manager_;
  std::unique_ptr<TestOrderGateway> order_gateway_;
  std::unique_ptr<TestTradeEngine> trade_engine_;
  std::unique_ptr<TestMarketConsumer> market_consumer_;
};

TEST_F(FullPipelineBenchmark, RealtimeReplay) {
  const std::string data_file = "data/benchmark/test_file.txt";
  auto benchmark_data = load_benchmark_file(data_file);

  std::cout << "\n=== Realtime Replay Benchmark ===" << std::endl;
  std::cout << "Data file: " << data_file << std::endl;
  std::cout << "Snapshot: " << (benchmark_data.snapshot.empty() ? "NO" : "YES")
            << std::endl;
  std::cout << "Stream messages: " << benchmark_data.stream_messages.size()
            << std::endl;

  market_consumer_->start();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Simulate connection -> triggers login flow -> state = kBuffering
  simulate_connection();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Inject snapshot via API transport -> state = kRunning
  if (!benchmark_data.snapshot.empty()) {
    inject_snapshot(benchmark_data.snapshot);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  // Replay stream messages with realtime timing
  constexpr double kSpeedMultiplier = 100.0;  // 100x speed
  uint64_t last_timestamp = 0;
  size_t msg_count = 0;

  auto start_time = std::chrono::high_resolution_clock::now();

  for (const auto& msg : benchmark_data.stream_messages) {
    // Extract timestamp from message (E field)
    uint64_t timestamp = 0;
    auto e_pos = msg.find("\"E\":");
    if (e_pos != std::string::npos) {
      e_pos += 4;
      while (e_pos < msg.size() && msg[e_pos] >= '0' && msg[e_pos] <= '9') {
        timestamp = timestamp * 10 + (msg[e_pos] - '0');
        ++e_pos;
      }
    }

    // Apply timing delay
    if (last_timestamp > 0 && timestamp > last_timestamp) {
      uint64_t delta_ms = timestamp - last_timestamp;
      auto delay = std::chrono::microseconds(
          static_cast<uint64_t>(delta_ms * 1000 / kSpeedMultiplier));
      std::this_thread::sleep_for(delay);
    }
    last_timestamp = timestamp;

    inject_stream(msg);
    ++msg_count;
  }

  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time)
                         .count();

  std::cout << "Messages replayed: " << msg_count << std::endl;

  // Wait for worker thread to finish processing
  std::this_thread::sleep_for(std::chrono::seconds(3));

  std::cout << "\n--- Replay Complete ---" << std::endl;
  std::cout << "Total replay time: " << duration_ms << " ms" << std::endl;
  std::cout << "Check benchmark_rdtsc.log for RDTSC measurements" << std::endl;

  EXPECT_GT(duration_ms, 0);
}

#endif  // USE_RING_BUFFER
