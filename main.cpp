/*
* MIT License
 *
 * Copyright (c) 2025 NewOro Corporation
 *
 * Permission is hereby granted, free of charge, to use, copy, modify, and
 * distribute this software for any purpose with or without fee, provided that
 * the above copyright notice appears in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 */

#include "logger.h"
#include "market_consumer.h"
#include "thread.hpp"
#include "trade_engine.h"

constexpr int kMarketUpdateDataPoolSize = 128;
constexpr int kMarketDataPoolSize = 2048;

int main() {
  std::unique_ptr<common::Logger> logger = std::make_unique<common::Logger>();
  logger->setLevel(common::LogLevel::kInfo);
  logger->clearSink();
  logger->addSink(std::make_unique<common::ConsoleSink>());

  auto market_update_data_pool =
      std::make_unique<common::MemoryPool<MarketUpdateData>>(
          kMarketUpdateDataPoolSize);
  auto market_data_pool =
      std::make_unique<common::MemoryPool<MarketData>>(kMarketDataPoolSize);

  std::promise<trading::TradeEngine*> engine_promise;
  std::future<trading::TradeEngine*> engine_future =
      engine_promise.get_future();

  common::Thread<common::PriorityTag<1>, common::AffinityTag<2>>
      trade_engine_thread;
  trade_engine_thread.start([&]() {
    auto engine = std::make_unique<trading::TradeEngine>(
        logger.get(), market_update_data_pool.get(), market_data_pool.get());
    engine_promise.set_value(engine.get());
    while (true) {}
  });

  common::Thread<common::PriorityTag<1>, common::AffinityTag<1>>
      consumer_thread;
  consumer_thread.start([&]() {
    auto* engine = engine_future.get();
    const trading::MarketConsumer consumer(logger.get(), engine,
                                           market_update_data_pool.get(),
                                           market_data_pool.get());
    while (true) {}
  });

  consumer_thread.join();
  trade_engine_thread.join();
  return 0;
}