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

#include "cpu_manager.h"
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
constexpr int kKilo = 1024;
constexpr int kThirty = 30;

int main() {
  try {
    INI_CONFIG.load("resources/config.ini");

    std::unique_ptr<common::Logger> logger = std::make_unique<common::Logger>();
    logger->setLevel(logger->string_to_level(INI_CONFIG.get("log", "level")));
    logger->clearSink();
    logger->addSink(std::make_unique<common::ConsoleSink>());
    logger->addSink(
        std::make_unique<common::FileSink>("log", kKilo * kKilo * kThirty));

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

    common::TradeEngineCfgHashMap config_map;
    config_map[INI_CONFIG.get("meta", "ticker")] = {
        .clip_ = common::Qty{0},
        .threshold_ = 0,
        .risk_cfg_ = common::RiskCfg(
            common::Qty{INI_CONFIG.get_double("risk", "max_order_size")},
            common::Qty{INI_CONFIG.get_double("risk", "max_position")},
            INI_CONFIG.get_double("risk", "max_loss"))};

    auto response_manager = std::make_unique<trading::ResponseManager>(
        logger.get(), execution_report_pool.get(),
        order_cancel_reject_pool.get(), order_mass_cancel_report_pool.get());

    auto order_gateway = std::make_unique<trading::OrderGateway>(
        logger.get(), response_manager.get());

    auto engine = std::make_unique<trading::TradeEngine>(
        logger.get(), market_update_data_pool.get(), market_data_pool.get(),
        response_manager.get(), config_map);
    engine->init_order_gateway(order_gateway.get());
    order_gateway->init_trade_engine(engine.get());

    const trading::MarketConsumer consumer(logger.get(), engine.get(),
                                           market_update_data_pool.get(),
                                           market_data_pool.get());

    std::unique_ptr<common::CpuManager> cpu_manager =
        std::make_unique<common::CpuManager>(logger.get());

    std::string cpu_init_result;
    if (cpu_manager->init_cpu_group(cpu_init_result)) {
      logger->info(std::format("don't init cpu group: {}", cpu_init_result));
    }

    if (cpu_manager->init_cpu_to_tid()) {
      logger->info("don't init cpu to tid");
    }

    constexpr int kSleepCount = 10;
    while (true)
      sleep(kSleepCount);  // TODO(neworo2):
  } catch (std::exception& e) {
    std::cerr << e.what() << "\n";
  }

  return 0;
}