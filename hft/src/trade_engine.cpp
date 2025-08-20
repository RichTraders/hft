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
#include "order_gateway.h"
#include "order_manager.h"
#include "performance.h"
#include "position_keeper.h"
#include "response_manager.h"
#include "risk_manager.h"

constexpr std::size_t kCapacity = 64;

namespace trading {
TradeEngine::TradeEngine(
    common::Logger* logger,
    common::MemoryPool<MarketUpdateData>* market_update_data_pool,
    common::MemoryPool<MarketData>* market_data_pool,
    ResponseManager* response_manager,
    const common::TradeEngineCfgHashMap& ticker_cfg)
    : logger_(logger),
      market_update_data_pool_(market_update_data_pool),
      market_data_pool_(market_data_pool),
      response_manager_(response_manager),
      queue_(std::make_unique<common::SPSCQueue<MarketUpdateData*>>(kCapacity)),
      feature_engine_(std::make_unique<FeatureEngine>(logger)),
      position_keeper_(std::make_unique<PositionKeeper>(logger)),
      risk_manager_(std::make_unique<RiskManager>(
          logger, position_keeper_.get(), ticker_cfg)),
      order_manager_(
          std::make_unique<OrderManager>(logger, this, *risk_manager_)) {
  auto orderbook = std::make_unique<MarketOrderBook>("BTCUSDT", logger);

  constexpr int kResponseQueueSize = 64;
  response_queue_ =
      std::make_unique<common::SPSCQueue<trading::ResponseCommon>>(
          kResponseQueueSize);
  orderbook->set_trade_engine(this);
  ticker_order_book_.insert({"BTCUSDT", std::move(orderbook)});

  thread_.start(&TradeEngine::run, this);
  response_thread_.start(&TradeEngine::response_run, this);
}

TradeEngine::~TradeEngine() {
  running_ = false;
  response_running_ = false;

  thread_.join();
  response_thread_.join();

  logger_->info("Trade Engine TEMarketData thread finish");
  logger_->info("Trade Engine TEResponse thread finish");
}

void TradeEngine::init_order_gateway(OrderGateway* order_gateway) {
  order_gateway_ = order_gateway;
}

void TradeEngine::on_market_data_updated(MarketUpdateData* data) const {
  queue_->enqueue(data);
}

void TradeEngine::stop() {
  running_ = false;
  response_running_ = false;
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

void TradeEngine::enqueue_response(const ResponseCommon& response) {
  response_queue_->enqueue(response);
}

void TradeEngine::send_request(const RequestCommon& request) {
  order_gateway_->order_request(request);
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

void TradeEngine::response_run() {
  while (response_running_) {
    ResponseCommon response;
    while (response_queue_->dequeue(response)) {
      START_MEASURE(RESPONSE_COMMON);
      switch (response.res_type) {
        case ResponseType::kExecutionReport:
          on_order_updated(response.execution_report);
          response_manager_->execution_report_deallocate(
              response.execution_report);
          break;
        case ResponseType::kOrderCancelReject:
          on_order_cancel_reject(response.order_cancel_reject);
          response_manager_->order_cancel_reject_deallocate(
              response.order_cancel_reject);
          break;
        case ResponseType::kOrderMassCancelReport:
          on_order_mass_cancel_report(response.order_mass_cancel_report);
          response_manager_->order_mass_cancel_report_deallocate(
              response.order_mass_cancel_report);
          break;
        case ResponseType::kInvalid:
        default:
          break;
      }

      END_MEASURE(RESPONSE_COMMON, logger_);
    }
    std::this_thread::yield();
  }
}

void TradeEngine::on_order_cancel_reject(const OrderCancelReject*) {
  logger_->info("on_order_cancel_reject");
}

void TradeEngine::on_order_mass_cancel_report(const OrderMassCancelReport*) {
  logger_->info("on_order_mass_cancel_report");
}

}  // namespace trading