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

#ifndef TRADE_ENGINE_H
#define TRADE_ENGINE_H

#include "market_data.h"
#include "memory_pool.hpp"
#include "order_entry.h"
#include "spsc_queue.h"
#include "thread.hpp"
#include "types.h"

#include "order_book.h"

namespace trading {
class PositionKeeper;
struct ExecutionReport;
class FeatureEngine;
class RiskManager;
class OrderManager;

class TradeEngine {
 public:
  explicit TradeEngine(
      common::Logger* logger,
      common::MemoryPool<MarketUpdateData>* market_update_data_pool,
      common::MemoryPool<MarketData>* market_data_pool,
      const common::TradeEngineCfgHashMap& ticker_cfg);
  ~TradeEngine();

  void stop();
  void on_market_data_updated(MarketUpdateData* data) const;
  void on_order_book_updated(common::Price price, common::Side side,
                             MarketOrderBook* market_order_book) const;
  void on_trade_updated(const MarketData* market_data,
                        MarketOrderBook* order_book) const;
  void on_order_updated(const ExecutionReport* report) const noexcept;
  void enqueue_response(const ResponseCommon& response);
  ResponseCommon dequeue_response();

  void send_request(const RequestCommon& request);

 private:
  common::Logger* logger_;
  common::MemoryPool<MarketUpdateData>* market_update_data_pool_;
  common::MemoryPool<MarketData>* market_data_pool_;
  std::unique_ptr<common::SPSCQueue<MarketUpdateData*>> queue_;
  common::Thread<common::AffinityTag<2>, common::PriorityTag<1>> thread_;
  std::unique_ptr<common::SPSCQueue<ResponseCommon>> response_queue_;

  MarketOrderBookHashMap ticker_order_book_;

  bool running_{true};
  std::unique_ptr<FeatureEngine> feature_engine_;
  std::unique_ptr<PositionKeeper> position_keeper_;
  std::unique_ptr<RiskManager> risk_manager_;
  std::unique_ptr<OrderManager> order_manager_;

  void run();
};
}  // namespace trading

#endif  //TRADE_ENGINE_H