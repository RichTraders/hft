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
#include "core/websocket/market_data/ws_md_decoder.h"
#include "core/websocket/market_data/ws_md_wire_message.h"
#include "common/logger.h"

// Helper to load file content
static std::string load_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + path);
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

static std::vector<char> load_binary_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + path);
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}


// --- WebSocket Benchmark ---
static void BM_WebSocket_Decode(benchmark::State& state) {
    common::Logger logger;
    logger.clearSink();
    auto producer = logger.make_producer();
    core::WsMdDecoder<core::JsonDecoderPolicy> decoder(producer);
    std::string json_data = load_file("data/benchmark/json.txt");

    for (auto _ : state) {
        auto wire_msg = decoder.decode(json_data);
        benchmark::DoNotOptimize(wire_msg);
    }
}
BENCHMARK(BM_WebSocket_Decode);