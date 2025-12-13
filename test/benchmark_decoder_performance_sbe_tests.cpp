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
#include "common/logger.h"
#include "websocket/market_data/sbe_md_decoder.hpp"
#include "websocket/market_data/exchanges/binance/spot/binance_spot_traits.h"

static std::vector<char> load_binary_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + path);
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// --- SBE Benchmark ---
static void BM_SBE_Decode(benchmark::State& state) {
    common::Logger logger;
    logger.clearSink();
    auto producer = logger.make_producer();
    core::SbeMdDecoder<BinanceSpotTraits> decoder(producer);
    auto binary_data = load_binary_file("data/benchmark/sbe.bin");

    for (auto _ : state) {
        auto wire_msg = decoder.decode({binary_data.data(), binary_data.size()});
        benchmark::DoNotOptimize(wire_msg);
    }
}
BENCHMARK(BM_SBE_Decode);
