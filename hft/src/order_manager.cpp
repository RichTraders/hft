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
      fast_clock_(kCpuHzEstimate, kInterval) {
  //TODO(JB): ticker 이름 받아오기
  ticker_side_order_["BTCUSDT"] = OMOrderSideHashMap{};
}

void OrderManager::on_order_updated(
    const ExecutionReport* client_response) noexcept {
  Order* order = find_order(client_response->symbol, client_response->side,
                            client_response->cl_order_id);
  if (UNLIKELY(!order)) {
    logger_->error("[CRITICAL]Order sent but, No order in the program!!!");
    return;
  }

  switch (client_response->ord_status) {
    case OrdStatus::kNew: {
      order->order_state = OMOrderState::kLive;
    } break;
    case OrdStatus::kPartiallyFilled: {
    } break;
    case OrdStatus::kFilled: {
      order->qty = client_response->leaves_qty;
      if (order->qty.value == 0.) {
        order->order_state = OMOrderState::kDead;
      }
      logger_->info(
          std::format("Completed order:{}", client_response->toString()));
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

void OrderManager::new_order(Order* order, const common::TickerId& ticker_id,
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

void OrderManager::move_order(Order* order, const common::TickerId& ticker_id,
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

void OrderManager::move_order(const common::TickerId& ticker_id,
                              const common::Price bid_price,
                              const common::Side side,
                              const common::Qty& qty) noexcept {
#ifdef MEASUREMENT
  START_MEASURE(Trading_OrderManager_moveOrder);
#endif
  Order* order = prepare_order(ticker_id, side);
  if (UNLIKELY(!order)) {
    logger_->error("No order available!!!");
    return;
  }
  move_order(order, ticker_id, bid_price, side, qty);
#ifdef MEASUREMENT
  END_MEASURE(Trading_OrderManager_moveOrder, logger_);
#endif
}
// NOLINTEND(bugprone-easily-swappable-parameters)

Order* OrderManager::find_order(const std::string& ticker, common::Side side,
                                common::OrderId order_id) {
  const auto iter = ticker_side_order_.find(ticker);
  if (iter == ticker_side_order_.end())
    return nullptr;

  const auto idx = common::sideToIndex(side);
  if (idx >= common::sideToIndex(common::Side::kTrade))
    return nullptr;

  auto& slots = iter->second[static_cast<std::size_t>(idx)];
  for (auto& ord : slots) {
    if (ord.order_state == OMOrderState::kDead)
      continue;
    if (ord.order_id == order_id)
      return &ord;
  }
  return nullptr;
}

Order* OrderManager::prepare_order(const std::string& ticker, common::Side side,
                                   bool create_if_missing) {
  const auto idx = common::sideToIndex(side);
  if (idx >= common::sideToIndex(common::Side::kTrade))
    return nullptr;

  if (create_if_missing) {
    auto& slots = ticker_side_order_[ticker][static_cast<std::size_t>(idx)];
    for (auto& ord : slots) {
      if (ord.order_state == OMOrderState::kDead)
        return &ord;
    }
    for (auto& ord : slots) {
      if (ord.order_state == OMOrderState::kInvalid)
        return &ord;
    }
    return nullptr;
  }
  const auto iter = ticker_side_order_.find(ticker);
  if (iter == ticker_side_order_.end())
    return nullptr;

  auto& slots = iter->second[static_cast<std::size_t>(idx)];
  for (auto& ord : slots) {
    if (ord.order_state == OMOrderState::kDead)
      return &ord;
  }
  for (auto& ord : slots) {
    if (ord.order_state == OMOrderState::kInvalid)
      return &ord;
  }
  return nullptr;
}
}  // namespace trading