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

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "hft/common/logger.h"
#include "hft/common/memory_pool.hpp"
#include "hft/core/NewOroFix44/fix_md_core.h"
#include "hft/src/gateway/market_data_gateway_interface.h"

namespace trading {
class TradeEngine;
}

struct MarketUpdateData;
struct MarketData;

namespace test {

/**
 * @brief Test market data gateway for performance testing without network I/O
 *
 * This gateway simulates market data subscription without connecting to a real server,
 * enabling file-based replay of FIX market data messages for NFR-PF performance tests.
 */
class TestMarketDataGateway : public trading::IMarketDataGateway {
 public:
  TestMarketDataGateway(common::Logger* logger, trading::TradeEngine* trade_engine,
                        common::MemoryPool<MarketUpdateData>* market_update_data_pool,
                        common::MemoryPool<MarketData>* market_data_pool);
  ~TestMarketDataGateway() override;

  void subscribe_market_data(const std::string& req_id, const std::string& depth,
                             const std::string& symbol, bool subscribe) override;
  void request_instrument_list(const std::string& symbol) override;
  void stop() override;

  /**
   * @brief Replay market data updates from FIX message file
   * @param messages Vector of FIX message strings (one per line)
   *
   * This method decodes FIX market data messages and feeds them to TradeEngine,
   * simulating live orderbook updates for performance testing.
   */
  void replay_market_data(const std::vector<std::string>& messages);

  /**
   * @brief Get the number of market data updates processed
   */
  size_t get_update_count() const { return update_count_; }

  /**
   * @brief Reset update counter
   */
  void reset_update_count() { update_count_ = 0; }

 private:
  common::MemoryPool<MarketUpdateData>* market_update_data_pool_;
  common::MemoryPool<MarketData>* market_data_pool_;
  common::Logger::Producer logger_;
  trading::TradeEngine* trade_engine_;
  std::unique_ptr<core::FixMdCore> decoder_;
  size_t update_count_{0};
};

}  // namespace test
