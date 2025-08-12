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

#include "hft/core/NewOroFix44/response_manager.h"
#include "ini_config.hpp"
#include "logger.h"
#include "market_consumer.h"
#include "order_entry.h"
#include "order_gateway.h"
#include "risk_manager.h"
#include "thread.hpp"
#include "trade_engine.h"

constexpr int kMarketUpdateDataPoolSize = 64;
constexpr int kMarketDataPoolSize = 16384;

int main() {
  try {
    IniConfig config;
#ifdef TEST_NET
    config.load("resources/test_config.ini");
#else
    config.load("resources/config.ini");
#endif

    const Authorization authorization{
        .md_address = config.get("auth", "md_address"),
        .oe_address = config.get("auth", "oe_address"),
        .port = config.get_int("auth", "port"),
        .api_key = config.get("auth", "api_key"),
        .pem_file_path = config.get("auth", "pem_file_path"),
        .private_password = config.get("auth", "private_password")};

    std::unique_ptr<common::Logger> logger = std::make_unique<common::Logger>();
    logger->setLevel(common::LogLevel::kInfo);
    logger->clearSink();
    logger->addSink(std::make_unique<common::ConsoleSink>());

    auto market_update_data_pool =
        std::make_unique<common::MemoryPool<MarketUpdateData>>(
            kMarketUpdateDataPoolSize);
    auto market_data_pool =
        std::make_unique<common::MemoryPool<MarketData>>(kMarketDataPoolSize);

    constexpr int kResponseMemoryPoolSize = 1024;

    auto execution_report_pool =
        std::make_unique<common::MemoryPool<trading::ExecutionReport>>(
            kResponseMemoryPoolSize);
    auto order_cancel_reject_pool =
        std::make_unique<common::MemoryPool<trading::OrderCancelReject>>(
            kResponseMemoryPoolSize);
    auto order_mass_cancel_report_pool =
        std::make_unique<common::MemoryPool<trading::OrderMassCancelReport>>(
            kResponseMemoryPoolSize);

    std::promise<trading::TradeEngine*> engine_promise;
    std::future<trading::TradeEngine*> engine_future =
        engine_promise.get_future();

    common::Thread<common::PriorityTag<1>, common::AffinityTag<2>>
        trade_engine_thread;
    trade_engine_thread.start([&]() {
      try {
        common::TradeEngineCfgHashMap config_map;
        config_map["BTCUSDT"] = {
            .clip_ = common::Qty{0},
            .threshold_ = 0,
            .risk_cfg_ = common::RiskCfg(
                common::Qty{static_cast<float>(
                    config.get_int("risk", "max_order_size"))},
                common::Qty{
                    static_cast<float>(config.get_int("risk", "max_position"))},
                config.get_int("risk", "max_loss"))};

        auto response_manager = std::make_unique<trading::ResponseManager>(
            logger.get(), execution_report_pool.get(),
            order_cancel_reject_pool.get(),
            order_mass_cancel_report_pool.get());

        auto order_gateway = std::make_unique<trading::OrderGateway>(
            authorization, logger.get(), response_manager.get());

        auto engine = std::make_unique<trading::TradeEngine>(
            logger.get(), market_update_data_pool.get(), market_data_pool.get(),
            response_manager.get(), config_map);
        engine_promise.set_value(engine.get());
        while (true) {}
      } catch (std::exception& e) {
        std::cerr << e.what() << "\n";
      }
    });

    common::Thread<common::PriorityTag<1>, common::AffinityTag<1>>
        consumer_thread;
    consumer_thread.start([&]() {
      try {
        auto* engine = engine_future.get();
        const trading::MarketConsumer consumer(
            logger.get(), engine, market_update_data_pool.get(),
            market_data_pool.get(), authorization);
        while (true) {}
      } catch (std::exception& e) {
        std::cerr << e.what() << "\n";
      }
    });

    consumer_thread.join();
    trade_engine_thread.join();
  } catch (std::exception& e) {
    std::cerr << e.what() << "\n";
  }
  return 0;
}