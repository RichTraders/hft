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

#include <pthread.h>
#include <csignal>
#include "common/fixed_point_config.hpp"
#include "precision_config.hpp"
#include "strategy_config.hpp"

using SelectedOrderGateway = trading::OrderGateway<>;
using SelectedTradeEngine = trading::TradeEngine<SelectedStrategy>;
using SelectedMarketConsumer = trading::MarketConsumer<SelectedStrategy>;

void block_all_signals(sigset_t& set) {
  sigfillset(&set);
  pthread_sigmask(SIG_BLOCK, &set, nullptr);
}
int main() {
  pthread_setname_np(pthread_self(), "hft_main");
  sigset_t set;
  block_all_signals(set);

  try {
    INI_CONFIG.load("resources/config.ini");
    PRECISION_CONFIG.initialize();

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
    config_map[INI_CONFIG.get("meta", "ticker")] = {
        .clip_ = common::QtyType::from_raw(0),
        .threshold_ = 0,
        .risk_cfg_ =
            common::RiskCfg(common::QtyType::from_int64(
                                INI_CONFIG.get_int64("risk", "max_order_size")),
                common::QtyType::from_int64(
                    INI_CONFIG.get_int64("risk", "max_position")),
                common::QtyType::from_int64(
                    INI_CONFIG.get_int64("risk", "min_position", 0)),
                INI_CONFIG.get_int64("risk", "max_loss") *
                    common::FixedPointConfig::kPnlScale)};

    auto log = logger->make_producer();

    auto response_manager = std::make_unique<trading::ResponseManager>(log,
        execution_report_pool.get(),
        order_cancel_reject_pool.get(),
        order_mass_cancel_report_pool.get());

    auto order_gateway =
        std::make_unique<SelectedOrderGateway>(log, response_manager.get());

    auto engine = std::make_unique<SelectedTradeEngine>(log,
        market_update_data_pool.get(),
        market_data_pool.get(),
        response_manager.get(),
        config_map);
    engine->init_order_gateway(order_gateway.get());
    order_gateway->init_trade_engine(engine.get());

    const auto consumer = std::make_unique<SelectedMarketConsumer>(log,
        engine.get(),
        market_update_data_pool.get(),
        market_data_pool.get());

    constexpr int kOeReadyTimeoutMs = 10000;
    constexpr int kPollIntervalMs = 100;
    int waited_ms = 0;
    while (!order_gateway->is_ready() && waited_ms < kOeReadyTimeoutMs) {
      std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
      waited_ms += kPollIntervalMs;
    }

    if (!order_gateway->is_ready()) {
      log.error("[Main] OE failed to become ready within {}ms timeout",
          kOeReadyTimeoutMs);
      return 1;
    }

    log.info("[Main] OE ready, starting MD");
    consumer->start();

    const auto cpu_manager = std::make_unique<common::CpuManager>(log);
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
