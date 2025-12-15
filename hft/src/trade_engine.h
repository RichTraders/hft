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

#include "common/logger.h"
#include "common/memory_pool.hpp"
#include "common/spsc_queue.h"
#include "common/thread.hpp"
#include "common/types.h"
#include "market_data.h"
#include "order_entry.h"
#include "protocol_impl.h"

namespace trading {
class PositionKeeper;
struct ExecutionReport;
template <typename Strategy, typename OeTraits>
class FeatureEngine;
class RiskManager;
template <typename Strategy, typename OeTraits>
class OrderManager;
class ResponseManager;

template <typename Strategy, typename OeTraits>
class OrderGateway;
template <typename Strategy, typename OeTraits>
class MarketOrderBook;
template <typename Strategy, typename OeTraits>
using MarketOrderBookHashMap =
    std::map<std::string, std::unique_ptr<MarketOrderBook<Strategy, OeTraits>>>;

constexpr std::size_t kMarketDataCapacity = 128;
constexpr int kResponseQueueSize = 64;

template <typename Strategy, typename OeTraits>
class TradeEngine {
 public:
  explicit TradeEngine(common::Logger* logger,
      common::MemoryPool<MarketUpdateData>* market_update_data_pool,
      common::MemoryPool<MarketData>* market_data_pool,
      ResponseManager* response_manager,
      const common::TradeEngineCfgHashMap& ticker_cfg)
    requires std::is_constructible_v<Strategy,
        OrderManager<Strategy, OeTraits>*,
        const FeatureEngine<Strategy, OeTraits>*,
        const common::Logger::Producer&, const common::TradeEngineCfgHashMap&>;
  ~TradeEngine();

  void init_order_gateway(OrderGateway<Strategy, OeTraits>* order_gateway);
  void stop();
  bool on_market_data_updated(MarketUpdateData* data);
  void on_orderbook_updated(const common::TickerId& ticker, common::Price price,
      common::Side side,
      MarketOrderBook<Strategy, OeTraits>* market_order_book);
  void on_trade_updated(const MarketData* market_data,
      MarketOrderBook<Strategy, OeTraits>* order_book);
  void on_order_updated(const ExecutionReport* report) noexcept;
  bool enqueue_response(const ResponseCommon& response);
  void send_request(const RequestCommon& request);
  void on_instrument_info(const InstrumentInfo& instrument_info);
  [[nodiscard]] double get_qty_increment() const { return qty_increment_; }

 private:
  using OeApp = protocol_impl::OrderEntryApp;
  static constexpr int kMarketDataBatchLimit = 128;
  static constexpr int kResponseBatchLimit = 64;
  static constexpr double kQtyDefault = 0.00001;
  common::Logger::Producer logger_;
  common::MemoryPool<MarketUpdateData>* market_update_data_pool_;
  common::MemoryPool<MarketData>* market_data_pool_;
  ResponseManager* response_manager_;
  OrderGateway<Strategy, OeTraits>* order_gateway_;
  std::unique_ptr<common::SPSCQueue<MarketUpdateData*, kMarketDataCapacity>>
      queue_;
  common::Thread<"TradeEngine"> thread_;
  std::unique_ptr<common::SPSCQueue<ResponseCommon, kResponseQueueSize>>
      response_queue_;
  MarketOrderBookHashMap<Strategy, OeTraits> ticker_order_book_;

  std::atomic<bool> running_{true};
  std::unique_ptr<FeatureEngine<Strategy, OeTraits>> feature_engine_;
  std::unique_ptr<PositionKeeper> position_keeper_;
  std::unique_ptr<RiskManager> risk_manager_;
  std::unique_ptr<OrderManager<Strategy, OeTraits>> order_manager_;

  Strategy strategy_;

  double qty_increment_{kQtyDefault};

  void run();
  void on_order_cancel_reject(const OrderCancelReject*);
  void on_order_mass_cancel_report(const OrderMassCancelReport*);
};
}  // namespace trading

#endif  //TRADE_ENGINE_H
