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
#include "core/fix/fix_md_core.h"
#include "common/logger.h"
#include "common/memory_pool.hpp"
#include "ini_config.hpp"
#include <fix8/f8includes.hpp>

static std::string load_file(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open file: " + path);
  }
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

class FixBenchmark : public benchmark::Fixture {
public:
    void SetUp(const ::benchmark::State& state) override {
        if (!fix) {
            INI_CONFIG.load("resources/config.ini");
            pool_ = std::make_unique<common::MemoryPool<MarketData>>(1024);
            logger_ = std::make_unique<common::Logger>();
            logger_->clearSink();
            fix = std::make_unique<core::FixMdCore>("SENDER", "TARGET", logger_.get(), pool_.get());
        }
    }

    void TearDown(const ::benchmark::State& state) override {
        // No need to reset unique_ptr, it will be cleaned up
    }

    static std::unique_ptr<common::Logger> logger_;
    static std::unique_ptr<core::FixMdCore> fix;
    static std::unique_ptr<common::MemoryPool<MarketData>> pool_;
};

std::unique_ptr<common::Logger> FixBenchmark::logger_;
std::unique_ptr<core::FixMdCore> FixBenchmark::fix;
std::unique_ptr<common::MemoryPool<MarketData>> FixBenchmark::pool_;

BENCHMARK_F(FixBenchmark, BM_FIX_Decode)(benchmark::State& state) {
  auto fix_data = load_file("data/binance_spot/benchmark/fix.txt");
    for (auto _ : state) {
        FIX8::Message* msg = fix->decode(fix_data);
        benchmark::DoNotOptimize(msg);
        delete msg;
    }
}
