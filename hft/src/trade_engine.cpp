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

#include "strategy/strategies.hpp"

constexpr int kMarketDataBatchLimit = 128;
constexpr int kResponseBatchLimit = 64;

namespace trading {
TradeEngine::TradeEngine(
    common::Logger* logger,
    common::MemoryPool<MarketUpdateData>* market_update_data_pool,
    common::MemoryPool<MarketData>* market_data_pool,
    ResponseManager* response_manager,
    const common::TradeEngineCfgHashMap& ticker_cfg)
    : logger_(logger->make_producer()),
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

  const std::string algorithm = INI_CONFIG.get("strategy", "algorithm");
  strategy_vtable_ = StrategyDispatch::instance().get_vtable(algorithm);

  if (!strategy_vtable_) {
    logger_.error(
        std::format("[Constructor] Failed to load strategy '{}'. Available "
                    "strategies: [{}]",
                    algorithm, [&]() {
                      std::string names;
                      for (const auto& name :
                           StrategyDispatch::instance().get_strategy_names()) {
                        if (!names.empty())
                          names += ", ";
                        names += name;
                      }
                      return names;
                    }()));
    throw std::runtime_error("Invalid strategy name in config: " + algorithm);
  }

  void* strategy_data = strategy_vtable_->create_data(
      order_manager_.get(), feature_engine_.get(), logger, ticker_cfg);
  strategy_context_ = std::make_unique<StrategyContext>(
      order_manager_.get(), feature_engine_.get(), logger, strategy_data);

  logger_.info(std::format("[Constructor] Strategy '{}' loaded successfully",
                           algorithm));

  thread_.start(&TradeEngine::run, this);
  logger_.info("[Constructor] TradeEngine Created");
}

TradeEngine::~TradeEngine() {
  running_ = false;

  thread_.join();

  if (strategy_vtable_ && strategy_context_ &&
      strategy_context_->strategy_data) {
    strategy_vtable_->destroy_data(strategy_context_->strategy_data);
    strategy_context_->strategy_data = nullptr;
  }
  logger_.info("[Thread] TradeEngine finish");
  logger_.info("[Destructor] TradeEngine Destroy");
}

void TradeEngine::init_order_gateway(OrderGateway* order_gateway) {
  order_gateway_ = order_gateway;
}

bool TradeEngine::on_market_data_updated(MarketUpdateData* data) const {
  return queue_->enqueue(data);
}

void TradeEngine::stop() {
  running_ = false;
}

void TradeEngine::on_orderbook_updated(const common::TickerId& ticker,
                                       common::Price price, common::Side side,
                                       MarketOrderBook* order_book) const {
  START_MEASURE(ORDERBOOK_UPDATED);
  feature_engine_->on_order_book_updated(price, side, order_book);
  strategy_vtable_->on_orderbook_updated(*strategy_context_, ticker, price,
                                         side, order_book);
  END_MEASURE(ORDERBOOK_UPDATED, logger_);
}

void TradeEngine::on_trade_updated(const MarketData* market_data,
                                   MarketOrderBook* order_book) const {
  START_MEASURE(TRADE_UPDATED);
  feature_engine_->on_trade_updated(market_data, order_book);
  strategy_vtable_->on_trade_updated(*strategy_context_, market_data,
                                     order_book);
  END_MEASURE(TRADE_UPDATED, logger_);
}

void TradeEngine::on_order_updated(const ExecutionReport* report) noexcept {
  START_MEASURE(Trading_TradeEngine_on_order_updated);
  position_keeper_->add_fill(report);
  strategy_vtable_->on_order_updated(*strategy_context_, report);
  order_manager_->on_order_updated(report);
  END_MEASURE(Trading_TradeEngine_on_order_updated, logger_);

  logger_.info(std::format("[OrderResult]{}", report->toString()));
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
    int md_processed = 0;
    MarketUpdateData* message;

    while (queue_->dequeue(message) && md_processed < kMarketDataBatchLimit) {
      if (UNLIKELY(message == nullptr))
        continue;
      wait.reset();
      START_MEASURE(MAKE_ORDERBOOK_ALL);
      for (const auto* market_data : message->data) {
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
      ++md_processed;
    }

    int resp_processed = 0;
    ResponseCommon response;
    while (response_queue_->dequeue(response) &&
           resp_processed < kResponseBatchLimit) {
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
      ++resp_processed;
    }

    if (md_processed == 0 && resp_processed == 0) {
      wait.idle_hot();
    }
  }
}

void TradeEngine::on_order_cancel_reject(const OrderCancelReject* reject) {
  logger_.info(
      std::format("[OrderResult]Order cancel request is rejected. error :{}",
                  reject->toString()));
}

void TradeEngine::on_order_mass_cancel_report(
    const OrderMassCancelReport* cancel_report) {
  logger_.info(
      std::format("[OrderResult]Order mass cancel is rejected. error:{}",
                  cancel_report->toString()));
}

}  // namespace trading