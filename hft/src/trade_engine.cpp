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

#include "trade_engine.h"
#include "feature_engine.h"
#include "order_entry.h"
#include "performance.h"
#include "position_keeper.h"
#include "risk_manager.h"

constexpr std::size_t kCapacity = 64;

namespace trading {
TradeEngine::TradeEngine(
    common::Logger* logger,
    common::MemoryPool<MarketUpdateData>* market_update_data_pool,
    common::MemoryPool<MarketData>* market_data_pool,
    const common::TradeEngineCfgHashMap& ticker_cfg)
    : logger_(logger),
      market_update_data_pool_(market_update_data_pool),
      market_data_pool_(market_data_pool),
      queue_(std::make_unique<common::SPSCQueue<MarketUpdateData*>>(kCapacity)),
      feature_engine_(std::make_unique<FeatureEngine>(logger)),
      position_keeper_(std::make_unique<PositionKeeper>(logger)),
      risk_manager_(std::make_unique<RiskManager>(
          logger, position_keeper_.get(), ticker_cfg)) {
  auto orderbook = std::make_unique<MarketOrderBook>("BTCUSDT", logger);
  orderbook->set_trade_engine(this);
  ticker_order_book_.insert({"BTCUSDT", std::move(orderbook)});

  thread_.start(&TradeEngine::run, this);
}

TradeEngine::~TradeEngine() = default;

void TradeEngine::on_market_data_updated(MarketUpdateData* data) const {
  queue_->enqueue(data);
}

void TradeEngine::stop() {
  running_ = false;
}

void TradeEngine::on_order_book_updated(common::Price price, common::Side side,
                                        MarketOrderBook* order_book) const {
  feature_engine_->on_order_book_updated(price, side, order_book);
}

void TradeEngine::on_trade_updated(const MarketData* market_data,
                                   MarketOrderBook* order_book) const {
  feature_engine_->on_trade_updated(market_data, order_book);
}

void TradeEngine::on_order_updated(
    const ExecutionReport* report) const noexcept {
  position_keeper_->add_fill(report);
}

void TradeEngine::run() {
  while (running_) {
    MarketUpdateData* message;
    while (queue_->dequeue(message)) {
      START_MEASURE(MAKE_ORDERBOOK);
      for (auto& market_data : message->data) {
        ticker_order_book_[market_data->ticker_id]->on_market_data_updated(
            market_data);
        market_data_pool_->deallocate(market_data);
      }

      if (message) {
        market_update_data_pool_->deallocate(message);
      }
      END_MEASURE(MAKE_ORDERBOOK, logger_);
    }
    std::this_thread::yield();
  }
}
}  // namespace trading