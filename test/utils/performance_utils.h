#pragma once

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

#include "hft/common/performance.h"

namespace test {

/**
 * @brief Statistics for latency measurements
 */
struct LatencyStats {
  uint64_t min_cycles{0};
  uint64_t p50_cycles{0};
  uint64_t p95_cycles{0};
  uint64_t p99_cycles{0};
  uint64_t max_cycles{0};
  double mean_cycles{0.0};
  size_t sample_count{0};

  /**
   * @brief Calculate statistics from a vector of cycle measurements
   * @param samples Vector of RDTSC cycle counts
   */
  void calculate(std::vector<uint64_t> samples) {
    if (samples.empty()) {
      return;
    }

    sample_count = samples.size();
    std::sort(samples.begin(), samples.end());

    min_cycles = samples.front();
    max_cycles = samples.back();

    // Calculate percentiles
    p50_cycles = samples[static_cast<size_t>(sample_count * 0.50)];
    p95_cycles = samples[static_cast<size_t>(sample_count * 0.95)];
    p99_cycles = samples[static_cast<size_t>(sample_count * 0.99)];

    // Calculate mean
    uint64_t sum = 0;
    for (const auto cycle : samples) {
      sum += cycle;
    }
    mean_cycles = static_cast<double>(sum) / static_cast<double>(sample_count);
  }

  /**
   * @brief Print a formatted report of the statistics
   * @param cpu_hz CPU frequency in Hz (e.g., 3.5e9 for 3.5 GHz)
   * @param label Optional label for the measurement
   */
  void print_report(double cpu_hz, const std::string& label = "") const {
    const double cycles_to_ns = 1e9 / cpu_hz;

    std::cout << "\n========================================\n";
    if (!label.empty()) {
      std::cout << "  " << label << "\n";
      std::cout << "========================================\n";
    }
    std::cout << "Samples:    " << sample_count << "\n";
    std::cout << "CPU:        " << std::fixed << std::setprecision(2) << (cpu_hz / 1e9)
              << " GHz\n";
    std::cout << "----------------------------------------\n";
    std::cout << "           Cycles          Nanoseconds\n";
    std::cout << "----------------------------------------\n";
    std::cout << "Min:       " << std::setw(8) << min_cycles << "     " << std::setw(10)
              << std::fixed << std::setprecision(2) << (min_cycles * cycles_to_ns) << " ns\n";
    std::cout << "Mean:      " << std::setw(8) << static_cast<uint64_t>(mean_cycles) << "     "
              << std::setw(10) << std::fixed << std::setprecision(2)
              << (mean_cycles * cycles_to_ns) << " ns\n";
    std::cout << "P50:       " << std::setw(8) << p50_cycles << "     " << std::setw(10)
              << std::fixed << std::setprecision(2) << (p50_cycles * cycles_to_ns) << " ns\n";
    std::cout << "P95:       " << std::setw(8) << p95_cycles << "     " << std::setw(10)
              << std::fixed << std::setprecision(2) << (p95_cycles * cycles_to_ns) << " ns\n";
    std::cout << "P99:       " << std::setw(8) << p99_cycles << "     " << std::setw(10)
              << std::fixed << std::setprecision(2) << (p99_cycles * cycles_to_ns) << " ns\n";
    std::cout << "Max:       " << std::setw(8) << max_cycles << "     " << std::setw(10)
              << std::fixed << std::setprecision(2) << (max_cycles * cycles_to_ns) << " ns\n";
    std::cout << "========================================\n\n";
  }

  /**
   * @brief Get latency in nanoseconds for a given percentile
   * @param cpu_hz CPU frequency in Hz
   * @param percentile Percentile to retrieve (50, 95, 99)
   * @return Latency in nanoseconds
   */
  double get_latency_ns(double cpu_hz, int percentile) const {
    const double cycles_to_ns = 1e9 / cpu_hz;
    switch (percentile) {
      case 50:
        return p50_cycles * cycles_to_ns;
      case 95:
        return p95_cycles * cycles_to_ns;
      case 99:
        return p99_cycles * cycles_to_ns;
      default:
        return mean_cycles * cycles_to_ns;
    }
  }
};

/**
 * @brief Benchmark runner for performance measurements
 *
 * This class provides a simple interface for running performance benchmarks
 * using RDTSC for high-precision timing.
 */
class PerformanceBenchmark {
 public:
  PerformanceBenchmark() { latencies_.reserve(10000); }

  /**
   * @brief Start a new measurement
   */
  void start() { start_cycle_ = common::rdtsc(); }

  /**
   * @brief End the current measurement and record the latency
   */
  void end() {
    const uint64_t end_cycle = common::rdtsc();
    if (end_cycle > start_cycle_) {
      latencies_.push_back(end_cycle - start_cycle_);
    }
  }

  /**
   * @brief Get the statistics for all recorded measurements
   * @return LatencyStats object with calculated statistics
   */
  LatencyStats get_stats() {
    LatencyStats stats;
    stats.calculate(latencies_);
    return stats;
  }

  /**
   * @brief Reset all recorded measurements
   */
  void reset() { latencies_.clear(); }

  /**
   * @brief Get the number of samples recorded
   */
  size_t sample_count() const { return latencies_.size(); }

  /**
   * @brief Reserve space for a certain number of samples
   * @param count Number of samples to reserve space for
   */
  void reserve(size_t count) { latencies_.reserve(count); }

 private:
  uint64_t start_cycle_{0};
  std::vector<uint64_t> latencies_;
};

/**
 * @brief RAII-style timer for automatic measurement
 *
 * Usage:
 *   PerformanceBenchmark bench;
 *   for (int i = 0; i < 1000; ++i) {
 *     ScopedTimer timer(bench);
 *     // Code to measure
 *   }
 */
class ScopedTimer {
 public:
  explicit ScopedTimer(PerformanceBenchmark& bench) : bench_(bench) { bench_.start(); }

  ~ScopedTimer() { bench_.end(); }

  // Disable copy
  ScopedTimer(const ScopedTimer&) = delete;
  ScopedTimer& operator=(const ScopedTimer&) = delete;

 private:
  PerformanceBenchmark& bench_;
};

}  // namespace test
