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

#include "hft_lib.h"

#include "strategy_config.hpp"
using SelectedOrderGateway =
    trading::OrderGateway<SelectedStrategy, SelectedOrderApp>;
using SelectedTradeEngine =
    trading::TradeEngine<SelectedStrategy, SelectedOrderApp>;
using SelectedMarketConsumer =
    trading::MarketConsumer<SelectedStrategy, SelectedMarketApp>;

int main() {
  try {
#ifdef TEST_NET
    INI_CONFIG.load("resources/test_config.ini");
#else
    INI_CONFIG.load("resources/config.ini");
#endif

    std::unique_ptr<common::Logger> logger = std::make_unique<common::Logger>();
    logger->setLevel(logger->string_to_level(INI_CONFIG.get("log", "level")));
    logger->clearSink();
    logger->addSink(std::make_unique<common::ConsoleSink>());
    logger->addSink(std::make_unique<common::FileSink>("log",
        INI_CONFIG.get_int("log", "size")));

    auto market_update_data_pool =
        std::make_unique<common::MemoryPool<MarketUpdateData>>(
            INI_CONFIG.get_int("main_init", "mud_pool_size"));
    auto market_data_pool = std::make_unique<common::MemoryPool<MarketData>>(
        INI_CONFIG.get_int("main_init", "md_pool_size"));

    const int k_response_memory_pool_size =
        INI_CONFIG.get_int("main_init", "response_memory_size");

    auto execution_report_pool =
        std::make_unique<common::MemoryPool<trading::ExecutionReport>>(
            k_response_memory_pool_size);
    auto order_cancel_reject_pool =
        std::make_unique<common::MemoryPool<trading::OrderCancelReject>>(
            k_response_memory_pool_size);
    auto order_mass_cancel_report_pool =
        std::make_unique<common::MemoryPool<trading::OrderMassCancelReport>>(
            k_response_memory_pool_size);

    common::TradeEngineCfgHashMap config_map;
    config_map[INI_CONFIG.get("meta", "ticker")] = {.clip_ = common::Qty{0},
        .threshold_ = 0,
        .risk_cfg_ = common::RiskCfg(
            common::Qty{INI_CONFIG.get_double("risk", "max_order_size")},
            common::Qty{INI_CONFIG.get_double("risk", "max_position")},
            common::Qty{INI_CONFIG.get_double("risk", "min_position", 0.)},
            INI_CONFIG.get_double("risk", "max_loss"))};

    auto response_manager =
        std::make_unique<trading::ResponseManager>(logger.get(),
            execution_report_pool.get(),
            order_cancel_reject_pool.get(),
            order_mass_cancel_report_pool.get());

    auto order_gateway = std::make_unique<SelectedOrderGateway>(logger.get(),
        response_manager.get());

    auto engine = std::make_unique<SelectedTradeEngine>(logger.get(),
        market_update_data_pool.get(),
        market_data_pool.get(),
        response_manager.get(),
        config_map);
    engine->init_order_gateway(order_gateway.get());
    order_gateway->init_trade_engine(engine.get());

    const SelectedMarketConsumer consumer(logger.get(),
        engine.get(),
        market_update_data_pool.get(),
        market_data_pool.get());

    std::unique_ptr<common::CpuManager> cpu_manager =
        std::make_unique<common::CpuManager>(logger.get());

    auto log = logger->make_producer();
    std::string cpu_init_result;
    if (cpu_manager->init_cpu_group(cpu_init_result)) {
      log.info(std::format("don't init cpu group: {}", cpu_init_result));
    }

    if (cpu_manager->init_cpu_to_tid()) {
      log.info("don't init cpu to tid");
    }

    constexpr int kSleepCount = 10;
    while (true)
      sleep(kSleepCount);  // TODO(neworo2):
  } catch (std::exception& e) {
    std::cerr << e.what() << "\n";
  }

  return 0;
}
