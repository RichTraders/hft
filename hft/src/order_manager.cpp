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
#include "logger.h"
#include "order_entry.h"
#include "performance.h"
#include "trade_engine.h"

namespace trading {
using common::OrderId;
constexpr double kCpuHzEstimate = 3.5e9;
constexpr int kInterval = 6;

OrderManager::OrderManager(common::Logger* logger, TradeEngine* trade_engine,
                           RiskManager& risk_manager)
    : trade_engine_(trade_engine),
      risk_manager_(risk_manager),
      logger_(logger),
      fast_clock_(kCpuHzEstimate, kInterval) {}

void OrderManager::on_order_updated(
    const ExecutionReport* client_response) noexcept {
  Order* order =
      &(ticker_side_order_[client_response->symbol][common::sideToIndex(
          client_response->side)][client_response->cl_order_id]);

  switch (client_response->ord_status) {
    case OrdStatus::kNew: {
      order->order_state = OMOrderState::kLive;
    } break;
    case OrdStatus::kPartiallyFilled: {
    } break;
    case OrdStatus::kFilled: {
      order->qty = client_response->leaves_qty;
      if (order->qty.value == 0.)
        order->order_state = OMOrderState::kDead;
    } break;
    case OrdStatus::kCanceled: {
      order->order_state = OMOrderState::kDead;
    } break;
    case OrdStatus::kPendingCancel: {
    } break;
    case OrdStatus::kRejected: {
      logger_->error(
          std::format("Rejected report:{}", client_response->toString()));
      order->order_state = OMOrderState::kDead;
    } break;
    case OrdStatus::kPendingNew: {
    }
    case OrdStatus::kExpired: {
      logger_->error(
          std::format("Expired report:{}", client_response->toString()));
      order->order_state = OMOrderState::kDead;
    } break;
  }
}

void OrderManager::new_order(Order* order, common::TickerId& ticker_id,
                             common::Price price, common::Side side,
                             common::Qty qty) noexcept {
  const RequestCommon new_request{
      .req_type = ReqeustType::kNewSingleOrderData,
      .cl_order_id = OrderId{fast_clock_.get_timestamp()},
      .symbol = ticker_id,
      .side = side,
      .order_qty = qty,
      .price = price};
  trade_engine_->send_request(new_request);

  *order = {ticker_id, new_request.cl_order_id,  side, price,
            qty,       OMOrderState::kPendingNew};

  logger_->info(std::format("Sent new order {} for {}", new_request.toString(),
                            order->toString()));
}

void OrderManager::cancel_order(Order* order) noexcept {
  const RequestCommon cancel_request{
      .req_type = ReqeustType::kOrderCancelRequest,
      .cl_order_id = OrderId{fast_clock_.get_timestamp()},
      .symbol = order->ticker_id};
  trade_engine_->send_request(cancel_request);

  order->order_state = OMOrderState::kPendingCancel;

  logger_->info(std::format("Sent cancel {} for {}", cancel_request.toString(),
                            order->toString()));
}

void OrderManager::move_order(Order* order, common::TickerId& ticker_id,
                              const common::Price price,
                              const common::Side side,
                              const common::Qty qty) noexcept {
  switch (order->order_state) {
    case OMOrderState::kLive: {
      if (order->price != price) {
#ifdef MEASUREMENT
        START_MEASURE(Trading_OrderManager_cancelOrder);
#endif
        cancel_order(order);
#ifdef MEASUREMENT
        END_MEASURE(Trading_OrderManager_cancelOrder, logger_);
#endif
      }
    } break;
    case OMOrderState::kInvalid:
    case OMOrderState::kDead: {
      if (LIKELY(price != common::kPriceInvalid)) {
#ifdef MEASUREMENT
        START_MEASURE(Trading_RiskManager_checkPreTradeRisk);
#endif
        const auto risk_result =
            risk_manager_.checkPreTradeRisk(ticker_id, side, qty);
#ifdef MEASUREMENT
        END_MEASURE(Trading_RiskManager_checkPreTradeRisk, logger_);
#endif
        if (LIKELY(risk_result == RiskCheckResult::kAllowed)) {
#ifdef MEASUREMENT
          START_MEASURE(Trading_OrderManager_newOrder);
#endif
          new_order(order, ticker_id, price, side, qty);
#ifdef MEASUREMENT
          END_MEASURE(Trading_OrderManager_newOrder, logger_);
#endif
        } else
          logger_->info(
              std::format("Ticker:{} Side:{} Qty:{} RiskCheckResult:{}",
                          ticker_id, toString(side), toString(qty),
                          riskCheckResultToString(risk_result)));
      }
    } break;
    case OMOrderState::kPendingNew:
    case OMOrderState::kPendingCancel:
      break;
  }
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
void OrderManager::move_orders(const std::vector<Order*>& orders,
                               common::TickerId& ticker_id,
                               const common::Price bid_price,
                               const common::Price ask_price,
                               const common::Qty clip) noexcept {
  {
    START_MEASURE(Trading_OrderManager_moveOrder);
    for (const auto& bid_order : orders) {
      move_order(bid_order, ticker_id, bid_price, common::Side::kBuy, clip);
    }
    END_MEASURE(Trading_OrderManager_moveOrder, logger_);
  }

  {
    START_MEASURE(Trading_OrderManager_moveOrder);
    for (const auto& ask_order : orders) {
      move_order(ask_order, ticker_id, ask_price, common::Side::kSell, clip);
    }
    END_MEASURE(Trading_OrderManager_moveOrder, logger_);
  }
}
// NOLINTEND(bugprone-easily-swappable-parameters)
}  // namespace trading