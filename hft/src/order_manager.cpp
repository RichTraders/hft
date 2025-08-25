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

#include "order_manager.h"
#include "ini_config.hpp"
#include "logger.h"
#include "order_entry.h"
#include "performance.h"
#include "trade_engine.h"

namespace trading {
using common::OrderId;

using common::Price;
using common::Qty;
using common::Side;
using common::TickerId;
using order::LayerBook;

OrderManager::OrderManager(common::Logger* logger, TradeEngine* trade_engine,
                           RiskManager& risk_manager)
    : layer_book_(
          INI_CONFIG.get("meta", "ticker")),  //TODO(JB): ticker 이름 받아오기
      trade_engine_(trade_engine),
      risk_manager_(risk_manager),
      logger_(logger),
      fast_clock_(INI_CONFIG.get_double("cpu_info", "clock"),
                  INI_CONFIG.get_int("cpu_info", "interval")) {
  logger_->info("[Constructor] OrderManager Construct");
}
OrderManager::~OrderManager() {
  logger_->info("[Destructor] OrderManager Destroy");
}

void OrderManager::on_order_updated(const ExecutionReport* response) noexcept {

  auto& side_book = layer_book_.side_book(response->symbol, response->side);
  int layer = LayerBook::find_layer_by_id(side_book, response->cl_order_id);
  if (layer < 0) {
    const uint64_t tick = to_ticks(
        response->price.value,
        INI_CONFIG.get_double(
            "meta", "ticker_size"));  // TODO(JB): 심볼별 tick_size 사용
    layer = LayerBook::find_layer_by_ticks(side_book, tick);
  }
  if (layer < 0) {
    logger_->error(
        std::format("[Updated] on_order_updated: layer not found. response={}",
                    response->toString()));
    return;
  }

  auto& slot = side_book.slots[layer];

  switch (response->ord_status) {
    case OrdStatus::kPendingNew: {
      slot.state = OMOrderState::kPendingNew;
      break;
    }
    case OrdStatus::kNew: {
      slot.state = OMOrderState::kLive;
      slot.price = response->price;
      slot.qty = response->leaves_qty;
      slot.cl_order_id = response->cl_order_id;
      logger_->info(std::format("[Updated] New {}", response->toString()));
      break;
    }
    case OrdStatus::kPartiallyFilled: {
      slot.qty = response->leaves_qty;
      slot.state = (response->leaves_qty.value <= 0.0) ? OMOrderState::kDead
                                                       : OMOrderState::kLive;
      if (slot.state == OMOrderState::kDead) {
        LayerBook::unmap_layer(side_book, layer);
      }
      logger_->info(
          std::format("[Updated] PartiallyFilled {}", response->toString()));
      break;
    }
    case OrdStatus::kFilled: {
      slot.qty = response->leaves_qty;  // 보통 0
      slot.state = OMOrderState::kDead;
      LayerBook::unmap_layer(side_book, layer);
      logger_->info(std::format("[Updated] Filled {}", response->toString()));
      break;
    }
    case OrdStatus::kPendingCancel: {
      slot.state = OMOrderState::kPendingCancel;
      break;
    }
    case OrdStatus::kCanceled: {
      slot.state = OMOrderState::kDead;
      LayerBook::unmap_layer(side_book, layer);
      logger_->info(std::format("[Updated] Canceled {}", response->toString()));
      break;
    }
    case OrdStatus::kRejected: {
      slot.state = OMOrderState::kDead;
      LayerBook::unmap_layer(side_book, layer);
      logger_->error(
          std::format("[Updated] Rejected {}", response->toString()));
      break;
    }
    case OrdStatus::kExpired: {
      slot.state = OMOrderState::kDead;
      LayerBook::unmap_layer(side_book, layer);
      logger_->error(std::format("[Updated] Expired {}", response->toString()));
      break;
    }
    default: {
      logger_->error(
          std::format("[Updated] on_order_updated: unknown OrdStatus {}",
                      toString(response->ord_status)));
      break;
    }
  }
}

void OrderManager::new_order(const TickerId& ticker_id, Price price, Side side,
                             Qty qty) noexcept {
  const RequestCommon new_request{
      .req_type = ReqeustType::kNewSingleOrderData,
      .cl_order_id = OrderId{fast_clock_.get_timestamp()},
      .symbol = ticker_id,
      .side = side,
      .order_qty = qty,
      .ord_type = OrderType::kLimit,
      .price = price,
      .time_in_force = TimeInForce::kGoodTillCancel};
  trade_engine_->send_request(new_request);

  logger_->info(std::format("Sent new order {}", new_request.toString()));
}

void OrderManager::modify_order(const TickerId& ticker_id,
                                const OrderId& order_id, Price price, Side side,
                                Qty qty) noexcept {
  const RequestCommon new_request{
      .req_type = ReqeustType::kOrderCancelRequestAndNewOrderSingle,
      .cl_order_id = OrderId{fast_clock_.get_timestamp()},
      .orig_cl_order_id = order_id,
      .symbol = ticker_id,
      .side = side,
      .order_qty = qty,
      .price = price};
  trade_engine_->send_request(new_request);

  logger_->info(
      std::format("[Request]Sent new order {}", new_request.toString()));
}

void OrderManager::cancel_order(const TickerId& ticker_id,
                                const OrderId& order_id) noexcept {
  const RequestCommon cancel_request{
      .req_type = ReqeustType::kOrderCancelRequest,
      .cl_order_id = OrderId{fast_clock_.get_timestamp()},
      .orig_cl_order_id = order_id,
      .symbol = ticker_id};
  trade_engine_->send_request(cancel_request);

  logger_->info(std::format("Sent cancel {}", cancel_request.toString()));
}

void OrderManager::apply(const std::vector<QuoteIntent>& intents) noexcept {
  START_MEASURE(Trading_OrderManager_apply);
  auto actions = order::QuoteReconciler::diff(
      intents, layer_book_, INI_CONFIG.get_double("meta", "ticker_size"),
      fast_clock_);
  filter_by_risk(intents, actions);

  const auto& ticker = intents.front().ticker;

  for (auto& action : actions.news) {
    new_order(ticker, action.price, action.side, action.qty);
  }
  for (auto& action : actions.repls) {
    modify_order(ticker, action.cl_order_id, action.price, action.side,
                 action.qty);
  }
  for (auto& action : actions.cancels) {
    cancel_order(ticker, action.cl_order_id);
  }
  END_MEASURE(Trading_OrderManager_apply, logger_);
}

void OrderManager::filter_by_risk(const std::vector<QuoteIntent>& intents,
                                  order::Actions& acts) const {
  const auto& ticker = intents.empty() ? std::string{} : intents.front().ticker;
  for (auto action = acts.news.begin(); action != acts.news.end();) {
    if (risk_manager_.checkPreTradeRisk(ticker, action->side, action->qty) ==
        RiskCheckResult::kAllowed) {
      ++action;
    } else {
      action = acts.news.erase(action);
    }
  }
  for (auto action = acts.repls.begin(); action != acts.repls.end();) {
    if (risk_manager_.checkPreTradeRisk(ticker, action->side,
                                        action->qty - action->last_qty) ==
        RiskCheckResult::kAllowed) {
      ++action;
    } else {
      action = acts.repls.erase(action);
    }
  }
}
}  // namespace trading