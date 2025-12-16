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

#include <csignal>
#include "strategy_config.hpp"

#ifdef USE_FUTURES_API
#include "core/websocket/order_entry/exchanges/binance/futures/binance_futures_oe_traits.h"
using SelectedOeTraits = BinanceFuturesOeTraits;
using SelectedStrategyType = SelectedStrategy<SelectedOeTraits>;
#else
#include "core/websocket/order_entry/exchanges/binance/spot/binance_spot_oe_traits.h"
using SelectedOeTraits = BinanceSpotOeTraits;
using SelectedStrategyType = SelectedStrategy<SelectedOeTraits>;
#endif

using SelectedOrderGateway =
    trading::OrderGateway<SelectedStrategyType, SelectedOeTraits>;
using SelectedTradeEngine =
    trading::TradeEngine<SelectedStrategyType, SelectedOeTraits>;
using SelectedMarketConsumer =
    trading::MarketConsumer<SelectedStrategyType, SelectedOeTraits>;

void block_all_signals(sigset_t& set) {
  sigfillset(&set);
  pthread_sigmask(SIG_BLOCK, &set, nullptr);
}
int main() {
  sigset_t set;
  block_all_signals(set);

  try {
    INI_CONFIG.load("resources/config.ini");

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

    const auto consumer = std::make_unique<SelectedMarketConsumer>(logger.get(),
        engine.get(),
        market_update_data_pool.get(),
        market_data_pool.get());

    const auto cpu_manager = std::make_unique<common::CpuManager>(logger.get());

    const auto log = logger->make_producer();
    std::string cpu_init_result;
    if (cpu_manager->init_cpu_group(cpu_init_result)) {
      log.info(std::format("don't init cpu group: {}", cpu_init_result));
    }

    if (cpu_manager->init_cpu_to_tid()) {
      log.info("don't init cpu to tid");
    }

    int sig;
    while (true) {
      sigwait(&set, &sig);

      if (sig == SIGINT || sig == SIGTERM) {
        std::cout << "\n[Main] Signal received\n";
        order_gateway->stop();
        consumer->stop();
        engine->stop();
        logger->shutdown();
        break;
      }
    }
  } catch (std::exception& e) {
    std::cerr << e.what() << "\n";
  }

  return 0;
}
