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
#include "ini_config.hpp"
#include "order_entry.h"
#include "order_gateway.h"
#include "order_manager.h"
#include "performance.h"
#include "position_keeper.h"
#include "response_manager.h"
#include "risk_manager.h"
#include "wait_strategy.h"

#include "strategy/market_maker.h"
constexpr int kWaitStrategyLimit = 4096;

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
      queue_(std::make_unique<
             common::SPSCQueue<MarketUpdateData*, kMarketDataCapacity>>()),
      feature_engine_(std::make_unique<FeatureEngine>(logger)),
      position_keeper_(std::make_unique<PositionKeeper>(logger)),
      risk_manager_(std::make_unique<RiskManager>(
          logger, position_keeper_.get(), ticker_cfg)),
      order_manager_(
          std::make_unique<OrderManager>(logger, this, *risk_manager_)) {
  const std::string ticker = INI_CONFIG.get("meta", "ticker");
  auto orderbook = std::make_unique<MarketOrderBook>(ticker, logger);
  response_queue_ = std::make_unique<
      common::SPSCQueue<trading::ResponseCommon, kResponseQueueSize>>();
  orderbook->set_trade_engine(this);
  ticker_order_book_.insert({ticker, std::move(orderbook)});

  strategy_ = std::make_unique<MarketMaker>(
      order_manager_.get(), feature_engine_.get(), logger_, ticker_cfg);

  thread_.start(&TradeEngine::run, this);
  response_thread_.start(&TradeEngine::response_run, this);
  logger_->info("[Constructor] TradeEngine Created");
}

TradeEngine::~TradeEngine() {
  running_ = false;
  response_running_ = false;

  thread_.join();
  response_thread_.join();

  logger_->info("[Thread] Trade Engine TEMarketData finish");
  logger_->info("[Thread] Trade Engine TEResponse finish");
  logger_->info("[Destructor] TradeEngine Destroy");
}

void TradeEngine::init_order_gateway(OrderGateway* order_gateway) {
  order_gateway_ = order_gateway;
}

bool TradeEngine::on_market_data_updated(MarketUpdateData* data) const {
  return queue_->enqueue(data);
}

void TradeEngine::stop() {
  running_ = false;
  response_running_ = false;
}

void TradeEngine::on_orderbook_updated(const common::TickerId& ticker,
                                       common::Price price, common::Side side,
                                       MarketOrderBook* order_book) const {
  START_MEASURE(ORDERBOOK_UPDATED);
  feature_engine_->on_order_book_updated(price, side, order_book);
  strategy_->on_orderbook_updated(ticker, price, side, order_book);
  END_MEASURE(ORDERBOOK_UPDATED, logger_);
}

void TradeEngine::on_trade_updated(const MarketData* market_data,
                                   MarketOrderBook* order_book) const {
  START_MEASURE(TRADE_UPDATED);
  feature_engine_->on_trade_updated(market_data, order_book);
  strategy_->on_trade_updated(market_data, order_book);
  END_MEASURE(TRADE_UPDATED, logger_);
}

void TradeEngine::on_order_updated(
    const ExecutionReport* report) const noexcept {
  START_MEASURE(Trading_TradeEngine_on_order_updated);
  position_keeper_->add_fill(report);
  strategy_->on_order_updated(report);
  order_manager_->on_order_updated(report);
  END_MEASURE(Trading_TradeEngine_on_order_updated, logger_);

  logger_->info(std::format("[OrderResult]{}", report->toString()));
}

bool TradeEngine::enqueue_response(const ResponseCommon& response) {
  return response_queue_->enqueue(response);
}

void TradeEngine::send_request(const RequestCommon& request) {
  order_gateway_->order_request(request);
}

void TradeEngine::run() {
  common::WaitStrategy wait;
  while (running_) {
    int processed = 0;
    MarketUpdateData* message;

    while (queue_->dequeue(message)) {
      if (UNLIKELY(message == nullptr))
        continue;
      START_MEASURE(MAKE_ORDERBOOK_ALL);
      for (auto& market_data : message->data) {
        START_MEASURE(MAKE_ORDERBOOK_UNIT);
        ticker_order_book_[market_data->ticker_id]->on_market_data_updated(
            market_data);
        market_data_pool_->deallocate(market_data);
        END_MEASURE(MAKE_ORDERBOOK_UNIT, logger_);
      }

      if (message) {
        market_update_data_pool_->deallocate(message);
      }
      END_MEASURE(MAKE_ORDERBOOK_ALL, logger_);
      ++processed;

      if (processed >= kWaitStrategyLimit)
        break;
    }

    if (processed == 0) {
      wait.idle_hot();
    }
  }
}

void TradeEngine::response_run() {
  common::WaitStrategy wait;
  while (response_running_) {
    int processed = 0;
    ResponseCommon response;
    while (response_queue_->dequeue(response)) {
      wait.reset();
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
      processed++;

      if (processed >= kWaitStrategyLimit)
        break;
    }
    if (processed == 0) {
      wait.idle_hot();
    }
  }
}

void TradeEngine::on_order_cancel_reject(const OrderCancelReject* reject) {
  logger_->info(
      std::format("[OrderResult]Order cancel request is rejected. error :{}",
                  reject->toString()));
}

void TradeEngine::on_order_mass_cancel_report(
    const OrderMassCancelReport* cancel_report) {
  logger_->info(
      std::format("[OrderResult]Order mass cancel is rejected. error:{}",
                  cancel_report->toString()));
}

}  // namespace trading