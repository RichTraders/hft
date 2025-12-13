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

#ifndef TRADE_ENGINE_TPP
#define TRADE_ENGINE_TPP

#include "trade_engine.h"

#include "core/response_manager.h"
#include "feature_engine.h"
#include "ini_config.hpp"
#include "order_entry.h"
#include "order_manager.h"
#include "performance.h"
#include "position_keeper.h"
#include "risk_manager.h"
#include "wait_strategy.h"

namespace trading {

template <typename Strategy>
TradeEngine<Strategy>::TradeEngine(common::Logger* logger,
    common::MemoryPool<MarketUpdateData>* market_update_data_pool,
    common::MemoryPool<MarketData>* market_data_pool,
    ResponseManager* response_manager,
    const common::TradeEngineCfgHashMap& ticker_cfg)
  requires std::is_constructible_v<Strategy, OrderManager<Strategy>*,
               const FeatureEngine<Strategy>*, const common::Logger::Producer&,
               const common::TradeEngineCfgHashMap&>
    : logger_(logger->make_producer()),
      market_update_data_pool_(market_update_data_pool),
      market_data_pool_(market_data_pool),
      response_manager_(response_manager),
      queue_(std::make_unique<
          common::SPSCQueue<MarketUpdateData*, kMarketDataCapacity>>()),
      feature_engine_(std::make_unique<FeatureEngine<Strategy>>(logger_)),
      position_keeper_(std::make_unique<PositionKeeper>(logger_)),
      risk_manager_(std::make_unique<RiskManager>(logger_,
          position_keeper_.get(), ticker_cfg)),
      order_manager_(std::make_unique<OrderManager<Strategy>>(logger_, this,
          *risk_manager_)),
      strategy_(order_manager_.get(), feature_engine_.get(), logger_,
          ticker_cfg) {
  const std::string ticker = INI_CONFIG.get("meta", "ticker");
  auto orderbook = std::make_unique<MarketOrderBook<Strategy>>(ticker, logger_);
  response_queue_ =
      std::make_unique<common::SPSCQueue<ResponseCommon, kResponseQueueSize>>();
  orderbook->set_trade_engine(this);
  ticker_order_book_.insert({ticker, std::move(orderbook)});

  thread_.start(&TradeEngine::run, this);
  logger_.info("[Constructor] TradeEngine Created");
}

template <typename Strategy>
TradeEngine<Strategy>::~TradeEngine() {
  running_.store(false, std::memory_order_release);
  thread_.join();
  // logger_.info("[Thread] TradeEngine finish");
  // logger_.info("[Destructor] TradeEngine Destroy");
  std::cout << "[Thread] TradeEngine finish\n";
  std::cout << "[Destructor] TradeEngine Destroy\n";
}

template <typename Strategy>
void TradeEngine<Strategy>::init_order_gateway(
    OrderGateway<Strategy>* order_gateway) {
  order_gateway_ = order_gateway;
}

template <typename Strategy>
bool TradeEngine<Strategy>::on_market_data_updated(MarketUpdateData* data) {
  return queue_->enqueue(data);
}

template <typename Strategy>
void TradeEngine<Strategy>::stop() {
  running_.store(false, std::memory_order_release);
}

template <typename Strategy>
void TradeEngine<Strategy>::on_orderbook_updated(const TickerId& ticker,
    Price price, Side side, MarketOrderBook<Strategy>* order_book) {
  START_MEASURE(ORDERBOOK_UPDATED);
  feature_engine_->on_order_book_updated(price, side, order_book);
  strategy_.on_orderbook_updated(ticker, price, side, order_book);
  END_MEASURE(ORDERBOOK_UPDATED, logger_);
}

template <typename Strategy>
void TradeEngine<Strategy>::on_trade_updated(const MarketData* market_data,
    MarketOrderBook<Strategy>* order_book) {
  START_MEASURE(TRADE_UPDATED);
  feature_engine_->on_trade_updated(market_data, order_book);
  strategy_.on_trade_updated(market_data, order_book);
  END_MEASURE(TRADE_UPDATED, logger_);
}

template <typename Strategy>
void TradeEngine<Strategy>::on_order_updated(
    const ExecutionReport* report) noexcept {
  START_MEASURE(Trading_TradeEngine_on_order_updated);
  position_keeper_->add_fill(report);
  strategy_.on_order_updated(report);
  order_manager_->on_order_updated(report);
  END_MEASURE(Trading_TradeEngine_on_order_updated, logger_);

  //logger_.info("[OrderResult]{}", report->toString());
}

template <typename Strategy>
bool TradeEngine<Strategy>::enqueue_response(const ResponseCommon& response) {
  return response_queue_->enqueue(response);
}

template <typename Strategy>
void TradeEngine<Strategy>::send_request(const RequestCommon& request) {
  order_gateway_->order_request(request);
}

template <typename Strategy>
void TradeEngine<Strategy>::run() {
  common::WaitStrategy wait;
  while (running_.load(std::memory_order_acquire)) {
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

template <typename Strategy>
void TradeEngine<Strategy>::on_order_cancel_reject(
    const OrderCancelReject* reject) {
  logger_.info("[OrderResult]Order cancel request is rejected. error :{}",
      reject->toString());
}

template <typename Strategy>
void TradeEngine<Strategy>::on_order_mass_cancel_report(
    const OrderMassCancelReport* cancel_report) {
  logger_.info("[OrderResult]Order mass cancel is rejected. error:{}",
      cancel_report->toString());
}

template <typename Strategy>
void TradeEngine<Strategy>::on_instrument_info(
    const InstrumentInfo& instrument_info) {
  if (!instrument_info.symbols.empty()) {
    qty_increment_ = instrument_info.symbols[0].min_qty_increment;
    logger_.info("[TradeEngine] Updated qty_increment to {}", qty_increment_);

    order_manager_->on_instrument_info(instrument_info);
  }
}
}  // namespace trading

#endif  // TRADE_ENGINE_TPP
