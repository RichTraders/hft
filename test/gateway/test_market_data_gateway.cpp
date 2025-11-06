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

#include "test_market_data_gateway.h"

#include <format>

#include "hft/src/trade_engine.h"

namespace test {

TestMarketDataGateway::TestMarketDataGateway(
    common::Logger* logger, trading::TradeEngine* trade_engine,
    common::MemoryPool<MarketUpdateData>* market_update_data_pool,
    common::MemoryPool<MarketData>* market_data_pool)
    : market_update_data_pool_(market_update_data_pool),
      market_data_pool_(market_data_pool),
      logger_(logger->make_producer()),
      trade_engine_(trade_engine),
      decoder_(std::make_unique<core::FixMdCore>(market_data_pool_)) {
  logger_.info("[Constructor] TestMarketDataGateway Constructor");
}

TestMarketDataGateway::~TestMarketDataGateway() {
  logger_.info("[Destructor] TestMarketDataGateway Destroy");
}

void TestMarketDataGateway::subscribe_market_data(const std::string& req_id,
                                                   const std::string& depth,
                                                   const std::string& symbol,
                                                   bool subscribe) {
  logger_.debug(std::format(
      "[TestMarketDataGateway] {} market data [req_id: {}, depth: {}, symbol: {}]",
      subscribe ? "Subscribe to" : "Unsubscribe from", req_id, depth, symbol));
}

void TestMarketDataGateway::request_instrument_list(const std::string& symbol) {
  logger_.debug(std::format(
      "[TestMarketDataGateway] Request instrument list for symbol: {}", symbol));
}

void TestMarketDataGateway::stop() {
  logger_.info(std::format(
      "[TestMarketDataGateway] Stopped (processed {} market data updates)",
      update_count_));
}

void TestMarketDataGateway::replay_market_data(
    const std::vector<std::string>& messages) {
  logger_.info(std::format("[TestMarketDataGateway] Replaying {} market data messages",
                           messages.size()));

  for (const auto& msg_str : messages) {
    try {
      ++update_count_;

      // Decode FIX message using existing FixMdCore
      FIX8::Message* msg = decoder_->decode(msg_str);
      if (!msg) {
        logger_.warn(
            std::format("[TestMarketDataGateway] Failed to decode message: {}", msg_str));
        continue;
      }

      // Check message type and process accordingly
      // 35=W: MarketDataSnapshotFullRefresh (Snapshot)
      // 35=X: MarketDataIncrementalRefresh (Updates)

      // For incremental updates (35=X)
      auto* market_update =
          market_update_data_pool_->allocate(decoder_->create_market_data_message(msg));

      if (market_update) {
        // Feed to TradeEngine
        if (!trade_engine_->on_market_data_updated(market_update)) {
          logger_.error(
              "[TestMarketDataGateway] Failed to send market data to TradeEngine");
        }
      } else {
        logger_.warn("[TestMarketDataGateway] Failed to allocate market update data");
      }

      delete msg;

    } catch (const std::exception& e) {
      logger_.error(
          std::format("[TestMarketDataGateway] Exception during replay: {}", e.what()));
    }
  }

  logger_.info(
      std::format("[TestMarketDataGateway] Replay complete ({} updates processed)",
                  update_count_));
}

}  // namespace test
