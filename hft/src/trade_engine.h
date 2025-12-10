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
#include <type_traits>

#include "logger.h"
#include "market_data.h"
#include "memory_pool.hpp"
#include "order_entry.h"
#include "spsc_queue.h"
#include "thread.hpp"
#include "types.h"

namespace trading {
class PositionKeeper;
struct ExecutionReport;
template <typename Strategy>
class FeatureEngine;
class RiskManager;
template <class Strategy>
class OrderManager;
class ResponseManager;

template <typename Strategy>
class OrderGateway;
template <typename Strategy>
class MarketOrderBook;
template <typename Strategy>
using MarketOrderBookHashMap =
    std::map<std::string, std::unique_ptr<MarketOrderBook<Strategy>>>;

constexpr std::size_t kMarketDataCapacity = 128;
constexpr int kResponseQueueSize = 64;

template <typename Strategy>
class TradeEngine {
 public:
  explicit TradeEngine(
      common::Logger* logger,
      common::MemoryPool<MarketUpdateData>* market_update_data_pool,
      common::MemoryPool<MarketData>* market_data_pool,
      ResponseManager* response_manager,
      const common::TradeEngineCfgHashMap& ticker_cfg)
    requires std::is_constructible_v<
        Strategy, OrderManager<Strategy>*, const FeatureEngine<Strategy>*,
        common::Logger*, const common::TradeEngineCfgHashMap&>;
  ~TradeEngine();

  void init_order_gateway(OrderGateway<Strategy>* order_gateway);
  void stop();
  bool on_market_data_updated(MarketUpdateData* data);
  void on_orderbook_updated(const common::TickerId& ticker, common::Price price,
                            common::Side side,
                            MarketOrderBook<Strategy>* market_order_book);
  void on_trade_updated(const MarketData* market_data,
                        MarketOrderBook<Strategy>* order_book);
  void on_order_updated(const ExecutionReport* report) noexcept;
  bool enqueue_response(const ResponseCommon& response);
  void send_request(const RequestCommon& request);
  void on_instrument_info(const InstrumentInfo& instrument_info);
  [[nodiscard]] double get_qty_increment() const { return qty_increment_; }

 private:
  static constexpr int kMarketDataBatchLimit = 128;
  static constexpr int kResponseBatchLimit = 64;
  static constexpr double kQtyDefault = 0.00001;
  common::Logger::Producer logger_;
  common::MemoryPool<MarketUpdateData>* market_update_data_pool_;
  common::MemoryPool<MarketData>* market_data_pool_;
  ResponseManager* response_manager_;
  OrderGateway<Strategy>* order_gateway_;
  std::unique_ptr<common::SPSCQueue<MarketUpdateData*, kMarketDataCapacity>>
      queue_;
  common::Thread<"TradeEngine"> thread_;
  std::unique_ptr<common::SPSCQueue<ResponseCommon, kResponseQueueSize>>
      response_queue_;
  MarketOrderBookHashMap<Strategy> ticker_order_book_;

  bool running_{true};
  std::unique_ptr<FeatureEngine<Strategy>> feature_engine_;
  std::unique_ptr<PositionKeeper> position_keeper_;
  std::unique_ptr<RiskManager> risk_manager_;
  std::unique_ptr<OrderManager<Strategy>> order_manager_;

  Strategy strategy_;

  double qty_increment_{kQtyDefault};

  void run();
  void on_order_cancel_reject(const OrderCancelReject*);
  void on_order_mass_cancel_report(const OrderMassCancelReport*);
};
}  // namespace trading

#endif  //TRADE_ENGINE_H
