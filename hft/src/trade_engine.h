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
class ResponseManager;
class OrderGateway;
class MarketMaker;

constexpr std::size_t kMarketDataCapacity = 128;
constexpr int kResponseQueueSize = 64;

class TradeEngine {
 public:
  explicit TradeEngine(
      common::Logger* logger,
      common::MemoryPool<MarketUpdateData>* market_update_data_pool,
      common::MemoryPool<MarketData>* market_data_pool,
      ResponseManager* response_manager,
      const common::TradeEngineCfgHashMap& ticker_cfg);
  ~TradeEngine();
  void init_order_gateway(OrderGateway* order_gateway);

  void stop();
  bool on_market_data_updated(MarketUpdateData* data) const;
  void on_orderbook_updated(const common::TickerId& ticker, common::Price price,
                            common::Side side,
                            MarketOrderBook* market_order_book) const;
  void on_trade_updated(const MarketData* market_data,
                        MarketOrderBook* order_book) const;
  void on_order_updated(const ExecutionReport* report) noexcept;
  bool enqueue_response(const ResponseCommon& response);

  void send_request(const RequestCommon& request);

 private:
  common::Logger::Producer logger_;
  common::MemoryPool<MarketUpdateData>* market_update_data_pool_;
  common::MemoryPool<MarketData>* market_data_pool_;
  ResponseManager* response_manager_;
  OrderGateway* order_gateway_;
  std::unique_ptr<common::SPSCQueue<MarketUpdateData*, kMarketDataCapacity>>
      queue_;
  common::Thread<"TEMarketData"> thread_;
  common::Thread<"TEResponse"> response_thread_;
  std::unique_ptr<common::SPSCQueue<ResponseCommon, kResponseQueueSize>>
      response_queue_;
  MarketOrderBookHashMap ticker_order_book_;

  bool running_{true};
  bool response_running_{true};
  std::unique_ptr<FeatureEngine> feature_engine_;
  std::unique_ptr<PositionKeeper> position_keeper_;
  std::unique_ptr<RiskManager> risk_manager_;
  std::unique_ptr<OrderManager> order_manager_;
  //TODO(JB): 전략 변화
  std::unique_ptr<MarketMaker> strategy_;

  void run();
  void response_run();
  void on_execution_report(const ExecutionReport*);
  void on_order_cancel_reject(const OrderCancelReject*);
  void on_order_mass_cancel_report(const OrderMassCancelReport*);
};
}  // namespace trading

#endif  //TRADE_ENGINE_H