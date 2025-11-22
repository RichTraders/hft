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

#ifndef ORDER_MANAGER_TPP
#define ORDER_MANAGER_TPP

#include <cmath>

#include "ini_config.hpp"
#include "order_entry.h"
#include "order_manager.h"
#include "performance.h"
#include "trade_engine.h"

namespace trading {
using common::OrderId;

using common::Price;
using common::Qty;
using common::Side;
using common::TickerId;
using order::LayerBook;
using order::PendingReplaceInfo;

template <typename Strategy>
OrderManager<Strategy>::OrderManager(common::Logger* logger,
                                     TradeEngine<Strategy>* trade_engine,
                                     RiskManager& risk_manager)
    : layer_book_(INI_CONFIG.get("meta", "ticker")),
      trade_engine_(trade_engine),
      risk_manager_(risk_manager),
      logger_(logger->make_producer()),
      fast_clock_(INI_CONFIG.get_double("cpu_info", "clock"),
                  INI_CONFIG.get_int("cpu_info", "interval")),
      ticker_size_(INI_CONFIG.get_double("meta", "ticker_size")),
      reconciler_(ticker_size_),
      ttl_reserved_ns_(INI_CONFIG.get_double("orders", "ttl_reserved_ns")),
      ttl_live_ns_(INI_CONFIG.get_double("orders", "ttl_live_ns")),
      tick_converter_(ticker_size_) {
  logger_.info("[Constructor] OrderManager Created");
}
template <typename Strategy>
OrderManager<Strategy>::~OrderManager() {
  logger_.info("[Destructor] OrderManager Destroy");
}

template <typename Strategy>
void OrderManager<Strategy>::on_order_updated(
    const ExecutionReport* response) noexcept {

  auto& side_book = layer_book_.side_book(response->symbol, response->side);

  switch (response->ord_status) {
    case OrdStatus::kPendingNew: {
      int layer = LayerBook::find_layer_by_id(side_book, response->cl_order_id);
      if (layer < 0) {
        const uint64_t tick = tick_converter_.to_ticks(response->price.value);
        layer = LayerBook::find_layer_by_ticks(side_book, tick);
      }
      if (layer < 0) {
        logger_.error(
            std::format("[OrderUpdated] PendingNew: layer not found {}",
                        response->toString()));
        break;
      }
      auto& slot = side_book.slots[layer];
      slot.state = OMOrderState::kPendingNew;
      break;
    }
    case OrdStatus::kNew: {
      int layer = LayerBook::find_layer_by_id(side_book, response->cl_order_id);
      {
        const auto iter =
            side_book.new_id_to_layer.find(response->cl_order_id.value);
        if (iter != side_book.new_id_to_layer.end())
          layer = iter->second;
      }
      if (layer < 0) {
        layer = LayerBook::find_layer_by_id(side_book, response->cl_order_id);
        if (layer < 0) {
          const uint64_t tick = tick_converter_.to_ticks(response->price.value);
          layer = LayerBook::find_layer_by_ticks(side_book, tick);
        }
        if (layer < 0) {
          logger_.error(std::format("[OrderUpdated] New: layer not found {}",
                                    response->toString()));
          break;
        }
      }
      {
        auto& new_slot = side_book.slots[layer];
        // Case: cancel and reorder
        if (auto& pend_opt = side_book.pending_repl[layer];
            pend_opt.has_value()) {
          const auto& pend = *pend_opt;

          side_book.layer_ticks[layer] = pend.new_tick;
          new_slot.price = response->price;
          new_slot.qty = response->leaves_qty;
          new_slot.cl_order_id = response->cl_order_id;
          new_slot.state = OMOrderState::kLive;
          side_book.pending_repl[layer].reset();
          side_book.new_id_to_layer.erase(response->cl_order_id.value);
        } else {
          // Case: general new order
          side_book.layer_ticks[layer] =
              tick_converter_.to_ticks(response->price.value);
          new_slot.price = response->price;
          new_slot.qty = response->leaves_qty;
          new_slot.cl_order_id = response->cl_order_id;
          new_slot.state = OMOrderState::kLive;
        }
      }
      register_expiry(response->symbol, response->side, layer,
                      response->cl_order_id, OMOrderState::kLive);
      logger_.info(std::format("[OrderUpdated] New {}", response->toString()));
      break;
    }
    case OrdStatus::kPartiallyFilled: {
      int layer = LayerBook::find_layer_by_id(side_book, response->cl_order_id);
      if (layer < 0) {
        const uint64_t tick = tick_converter_.to_ticks(response->price.value);
        layer = LayerBook::find_layer_by_ticks(side_book, tick);
      }
      if (layer < 0) {
        logger_.error(
            std::format("[OrderUpdated] PartiallyFilled: layer not found {}",
                        response->toString()));
        break;
      }
      auto& slot = side_book.slots[layer];
      reserved_position_ -= common::sideToValue(response->side) *
                            (slot.qty - response->leaves_qty);
      slot.qty = response->leaves_qty;
      slot.state = (response->leaves_qty.value <= 0.0) ? OMOrderState::kDead
                                                       : OMOrderState::kLive;
      if (slot.state == OMOrderState::kDead) {
        LayerBook::unmap_layer(side_book, layer);
      } else {
        slot.last_used = fast_clock_.get_timestamp();
        register_expiry(response->symbol, response->side, layer,
                        response->cl_order_id, OMOrderState::kLive);
      }
      logger_.info(std::format("[OrderUpdated] PartiallyFilled {}",
                               response->toString()));
      break;
    }
    case OrdStatus::kFilled: {
      int layer = LayerBook::find_layer_by_id(side_book, response->cl_order_id);
      if (layer < 0) {
        const uint64_t tick = tick_converter_.to_ticks(response->price.value);
        layer = LayerBook::find_layer_by_ticks(side_book, tick);
      }
      if (layer < 0) {
        logger_.error(std::format("[OrderUpdated] Filled: layer not found {}",
                                  response->toString()));
        break;
      }
      auto& slot = side_book.slots[layer];
      reserved_position_ -= common::sideToValue(response->side) * slot.qty;
      slot.qty = response->leaves_qty;
      slot.state = OMOrderState::kDead;
      LayerBook::unmap_layer(side_book, layer);

      logger_.info(
          std::format("[OrderUpdated] Filled {}", response->toString()));
      break;
    }
    case OrdStatus::kPendingCancel: {
      int layer = LayerBook::find_layer_by_id(side_book, response->cl_order_id);
      if (layer < 0) {
        const uint64_t tick = tick_converter_.to_ticks(response->price.value);
        layer = LayerBook::find_layer_by_ticks(side_book, tick);
      }
      if (layer < 0) {
        logger_.error(
            std::format("[OrderUpdated] PendingCancel: layer not found {}",
                        response->toString()));
        break;
      }
      auto& slot = side_book.slots[layer];
      slot.state = OMOrderState::kPendingCancel;
      break;
    }
    case OrdStatus::kCanceled: {
      int layer;
      if (const auto iter =
              side_book.orig_id_to_layer.find(response->cl_order_id.value);
          iter != side_book.orig_id_to_layer.end()) {
        layer = iter->second;
        side_book.orig_id_to_layer.erase(iter);
        auto& slot = side_book.slots[layer];
        slot.state = OMOrderState::kReserved;
        logger_.info(std::format("[OrderUpdated] Canceled (for replace) {}",
                                 response->toString()));
        break;
      }

      layer = LayerBook::find_layer_by_id(side_book, response->cl_order_id);
      if (layer < 0) {
        const uint64_t tick = tick_converter_.to_ticks(response->price.value);
        layer = LayerBook::find_layer_by_ticks(side_book, tick);
      }
      if (layer < 0) {
        logger_.error(std::format("[OrderUpdated] Canceled: layer not found {}",
                                  response->toString()));
        break;
      }

      auto& slot = side_book.slots[layer];
      reserved_position_ -= common::sideToValue(response->side) * slot.qty;
      slot.state = OMOrderState::kDead;
      LayerBook::unmap_layer(side_book, layer);

      logger_.info(std::format("[OrderUpdated] Canceled {}",
                               response->toString()));
      break;
    }
    case OrdStatus::kRejected:
      [[fallthrough]];
    case OrdStatus::kExpired: {
      int layer = -1;
      if (const auto iter =
              side_book.new_id_to_layer.find(response->cl_order_id.value);
          iter != side_book.new_id_to_layer.end()) {
        layer = iter->second;
      }

      if (const auto& pend_opt = side_book.pending_repl[layer];
          layer >= 0 && pend_opt.has_value()) {
        const auto& pend = *pend_opt;
        reserved_position_ -= common::sideToValue(response->side) *
                              (pend.new_qty - pend.last_qty);
        side_book.pending_repl[layer].reset();
        if (const auto iter =
                side_book.new_id_to_layer.find(response->cl_order_id.value);
            iter != side_book.new_id_to_layer.end()) {
          side_book.new_id_to_layer.erase(iter);
        }

        auto& slot = side_book.slots[layer];
        slot.state = OMOrderState::kLive;
        slot.price = pend.original_price;
        slot.cl_order_id = pend.original_cl_order_id;
        slot.qty = pend.last_qty;
        side_book.layer_ticks[layer] = pend.original_tick;

        const auto cancel_id = response->cl_order_id.value - 1;
        if (const auto iter = side_book.orig_id_to_layer.find(cancel_id);
            iter != side_book.orig_id_to_layer.end()) {
          side_book.orig_id_to_layer.erase(iter);
        }

        logger_.info(std::format("[OrderUpdated] Rejected (replace failed, restored original oid={}, price={:.2f}, qty={:.6f}) {}",
                                 pend.original_cl_order_id.value, pend.original_price.value,
                                 pend.last_qty.value, response->toString()));
      } else {
        layer = LayerBook::find_layer_by_id(side_book, response->cl_order_id);
        if (layer < 0) {
          const uint64_t tick = tick_converter_.to_ticks(response->price.value);
          layer = LayerBook::find_layer_by_ticks(side_book, tick);
        }
        if (layer >= 0) {
          auto& slot = side_book.slots[layer];
          reserved_position_ -= common::sideToValue(response->side) * slot.qty;
          slot.state = OMOrderState::kDead;
          LayerBook::unmap_layer(side_book, layer);
        } else {
          logger_.error(std::format("[OrderUpdated] {}: layer not found {}",
                                    trading::toString(response->ord_status),
                                    response->toString()));
        }
      }

      logger_.error(std::format("[OrderUpdated] {} {}",
                                trading::toString(response->ord_status),
                                response->toString()));
      break;
    }
    default: {
      logger_.error(
          std::format("[OrderUpdated] on_order_updated: unknown OrdStatus {}",
                      toString(response->ord_status)));
      break;
    }
  }

  constexpr double kReservedPositionEpsilon = 1e-8;
  if (std::abs(reserved_position_.value) < kReservedPositionEpsilon) {
    reserved_position_.value = 0.0;
  }

  logger_.debug(
      std::format("[OrderUpdated]Order Id:{} reserved_position:{:.6f}",
                  response->cl_order_id.value, reserved_position_.value));

  dump_all_slots(response->symbol,
                 std::format("After {} oid={}",
                             toString(response->ord_status),
                             response->cl_order_id.value));
}

template <typename Strategy>
void OrderManager<Strategy>::new_order(const TickerId& ticker_id,
                                       const Price price, const Side side,
                                       const Qty qty,
                                       const OrderId order_id) noexcept {
  const RequestCommon new_request{
      .req_type = ReqeustType::kNewSingleOrderData,
      .cl_order_id = order_id,
      .symbol = ticker_id,
      .side = side,
      .order_qty = qty,
      .ord_type = OrderType::kLimit,
      .price = price,
      .time_in_force = TimeInForce::kGoodTillCancel};
  trade_engine_->send_request(new_request);

  logger_.info(
      std::format("[OrderRequest]Sent new order {}", new_request.toString()));
}

template <typename Strategy>
void OrderManager<Strategy>::modify_order(const TickerId& ticker_id,
                                          const OrderId& cancel_new_order_id,
                                          const OrderId& order_id,
                                          const OrderId& original_order_id,
                                          Price price, Side side,
                                          const Qty qty) noexcept {
  const RequestCommon new_request{
      .req_type = ReqeustType::kOrderCancelRequestAndNewOrderSingle,
      .cl_cancel_order_id = cancel_new_order_id,
      .cl_order_id = order_id,
      .orig_cl_order_id = original_order_id,
      .symbol = ticker_id,
      .side = side,
      .order_qty = qty,
      .ord_type = OrderType::kLimit,
      .price = price,
      .time_in_force = TimeInForce::kGoodTillCancel};
  trade_engine_->send_request(new_request);

  logger_.info(std::format("[OrderRequest]Sent modify order {}",
                           new_request.toString()));
}

template <typename Strategy>
void OrderManager<Strategy>::cancel_order(const TickerId& ticker_id,
                                          const OrderId& original_order_id,
                                          const OrderId& order_id) noexcept {
  const RequestCommon cancel_request{
      .req_type = ReqeustType::kOrderCancelRequest,
      .cl_order_id = order_id,
      .orig_cl_order_id = original_order_id,
      .symbol = ticker_id};
  trade_engine_->send_request(cancel_request);

  logger_.info(
      std::format("[OrderRequest]Sent cancel {}", cancel_request.toString()));
}

template <typename Strategy>
void OrderManager<Strategy>::on_instrument_info(
    const InstrumentInfo& instrument_info) noexcept {
  if (!instrument_info.symbols.empty()) {
    venue_policy_.set_qty_increment(
        instrument_info.symbols[0].min_qty_increment);
    logger_.info(std::format("[OrderManager] Updated qty_increment to {}",
                             instrument_info.symbols[0].min_qty_increment));
  }
}

template <typename Strategy>
void OrderManager<Strategy>::apply(
    const std::vector<QuoteIntent>& intents) noexcept {
  START_MEASURE(Trading_OrderManager_apply);

  auto actions = reconciler_.diff(intents, layer_book_, fast_clock_);

  if (intents.empty()) {
    sweep_expired();
    END_MEASURE(Trading_OrderManager_apply, logger_);
    return;
  }

  const auto& ticker = intents.front().ticker;

  venue_policy_.filter_by_venue(ticker, actions, fast_clock_.get_timestamp(),
                                layer_book_);
  filter_by_risk(intents, actions);

  for (auto& action : actions.news) {
    auto& side_book = layer_book_.side_book(ticker, action.side);
    auto& [state, price, qty, last_used, cl_order_id] =
        side_book.slots[action.layer];

    const uint64_t tick = tick_converter_.to_ticks(action.price.value);
    if (const int existing = LayerBook::find_layer_by_ticks(side_book, tick);
        existing >= 0 && existing != action.layer) {
      continue;
    }

    side_book.layer_ticks[action.layer] = tick;
    price = action.price;
    qty = action.qty;
    cl_order_id = action.cl_order_id;
    state = OMOrderState::kReserved;
    last_used = fast_clock_.get_timestamp();

    new_order(ticker, action.price, action.side, action.qty,
              action.cl_order_id);
    reserved_position_ += common::sideToValue(action.side) * action.qty;

    logger_.info(std::format(
        "[Apply][NEW] tick:{}/ layer={}, side:{}, order_id={}, "
        "reserved_position_={}",
        tick, action.layer, common::toString(action.side),
        common::toString(action.cl_order_id), reserved_position_.value));

    register_expiry(ticker, action.side, action.layer, action.cl_order_id,
                    OMOrderState::kReserved);
  }
  for (auto& action : actions.repls) {
    auto& side_book = layer_book_.side_book(ticker, action.side);
    auto& slot = side_book.slots[action.layer];

    const uint64_t tick = tick_converter_.to_ticks(action.price.value);
    if (const int existing = LayerBook::find_layer_by_ticks(side_book, tick);
        existing >= 0 && existing != action.layer) {
      continue;
    }

    const auto original_price = slot.price;
    const auto original_tick = side_book.layer_ticks[action.layer];

    side_book.layer_ticks[action.layer] = tick;
    slot.price = action.price;
    slot.qty = action.qty;
    slot.cl_order_id = action.cl_order_id;
    slot.state = OMOrderState::kCancelReserved;
    slot.last_used = fast_clock_.get_timestamp();

    for (auto iter = side_book.new_id_to_layer.begin();
         iter != side_book.new_id_to_layer.end();) {
      if (iter->second == action.layer) {
        const auto to_erase = iter;
        ++iter;
        side_book.new_id_to_layer.erase(to_erase);
      } else
        ++iter;
    }
    const auto cancel_new_order_id = OrderId{action.cl_order_id.value - 1};

    side_book.orig_id_to_layer[cancel_new_order_id.value] = action.layer;
    side_book.new_id_to_layer[action.cl_order_id.value] = action.layer;
    side_book.pending_repl[action.layer] = PendingReplaceInfo{
        action.price, action.qty, tick, action.cl_order_id, action.last_qty,
        action.original_cl_order_id, original_price, original_tick};
    modify_order(ticker, cancel_new_order_id, action.cl_order_id,
                 action.original_cl_order_id, action.price, action.side,
                 action.qty);

    reserved_position_ +=
        common::sideToValue(action.side) * (action.qty - action.last_qty);
    logger_.info(std::format(
        "[Apply][REPLACE] tick:{}/ layer={}, side:{}, order_id={}, "
        "reserved_position_={}",
        tick, action.layer, common::toString(action.side),
        common::toString(action.cl_order_id), reserved_position_.value));
    register_expiry(ticker, action.side, action.layer, action.cl_order_id,
                    OMOrderState::kCancelReserved);
  }
  for (auto& action : actions.cancels) {
    auto& side_book = layer_book_.side_book(ticker, action.side);
    auto& slot = side_book.slots[action.layer];
    slot.state = OMOrderState::kCancelReserved;
    slot.last_used = fast_clock_.get_timestamp();
    cancel_order(ticker, action.original_cl_order_id, action.cl_order_id);
    logger_.info(std::format(
        "[Apply][CANCEL] layer={}, side:{}, order_id={}, "
        "reserved_position_={}",
        "previous order id :{}", action.layer, common::toString(action.side),
        common::toString(action.cl_order_id),
        common::toString(action.original_cl_order_id),
        reserved_position_.value));
  }
  sweep_expired();
  END_MEASURE(Trading_OrderManager_apply, logger_);
}

template <typename Strategy>
void OrderManager<Strategy>::filter_by_risk(
    const std::vector<QuoteIntent>& intents, order::Actions& acts) {
  const auto& ticker = intents.empty() ? std::string{} : intents.front().ticker;
  auto running = reserved_position_;
  auto allow_new = [&](auto& actions) {
    for (auto action = actions.begin(); action != actions.end();) {
      const auto delta = action->qty;
      if (risk_manager_.check_pre_trade_risk(ticker, action->side, delta,
                                             running) ==
          RiskCheckResult::kAllowed) {
        running += common::sideToValue(action->side) * delta.value;
        ++action;
      } else {
        action = actions.erase(action);
      }
    }
  };
  auto allow_repl = [&](auto& actions) {
    for (auto action = actions.begin(); action != actions.end();) {
      const auto delta = action->qty - action->last_qty;
      if (risk_manager_.check_pre_trade_risk(ticker, action->side, delta,
                                             running) ==
          RiskCheckResult::kAllowed) {
        running += common::sideToValue(action->side) * delta.value;
        ++action;
      } else {
        action = actions.erase(action);
      }
    }
  };

  allow_new(acts.news);
  allow_repl(acts.repls);
}

template <typename Strategy>
void OrderManager<Strategy>::register_expiry(const TickerId& ticker, Side side,
                                             uint32_t layer,
                                             const OrderId& order_id,
                                             OMOrderState state) noexcept {
  const auto now = fast_clock_.get_timestamp();
  const auto ttl = (state == OMOrderState::kReserved ||
                    state == OMOrderState::kCancelReserved)
                       ? ttl_reserved_ns_
                       : ttl_live_ns_;
  expiry_pq_.push(ExpiryKey{.expire_ts = now + ttl,
                            .symbol = ticker,
                            .side = side,
                            .layer = layer,
                            .cl_order_id = order_id});
}

template <typename Strategy>
void OrderManager<Strategy>::sweep_expired() noexcept {
  const auto now = fast_clock_.get_timestamp();

  while (!expiry_pq_.empty() && expiry_pq_.top().expire_ts <= now) {
    const auto key = expiry_pq_.top();
    expiry_pq_.pop();

    auto& side_book = layer_book_.side_book(key.symbol, key.side);
    if (UNLIKELY(key.layer >= side_book.slots.size()))
      continue;

    auto& slot = side_book.slots[key.layer];
    if (slot.cl_order_id != key.cl_order_id) {
      continue;
    }

    if (slot.state == OMOrderState::kDead ||
        slot.state == OMOrderState::kCancelReserved) {
      continue;
    }

    if (slot.state == OMOrderState::kLive ||
        slot.state == OMOrderState::kReserved) {
      const auto cancel_id = gen_order_id();
      slot.state = OMOrderState::kCancelReserved;
      slot.last_used = now;

      cancel_order(key.symbol, slot.cl_order_id, cancel_id);

      logger_.info(
          std::format("[TTL] Cancel sent (state={}, layer={}, oid={}, "
                      "cancel_id={}, remaining_ns={})",
                      trading::toString(slot.state), key.layer,
                      common::toString(slot.cl_order_id),
                      common::toString(cancel_id), key.expire_ts - now));
    }
  }
}

template <typename Strategy>
OrderId OrderManager<Strategy>::gen_order_id() noexcept {
  const auto now = fast_clock_.get_timestamp();
  return OrderId{now};
}

template <typename Strategy>
void OrderManager<Strategy>::dump_all_slots(const std::string& symbol, const std::string& context) noexcept {
  logger_.debug(std::format("[SLOT_DUMP] ========== {} ==========", context));
  logger_.debug(std::format("[SLOT_DUMP] Symbol: {}, Reserved: {:.10f}", symbol, reserved_position_.value));

  for (int side_idx = 0; side_idx < 2; ++side_idx) {
    const auto side = side_idx == 0 ? common::Side::kBuy : common::Side::kSell;
    const auto& side_book = layer_book_.side_book(symbol, side);

    logger_.debug(std::format("[SLOT_DUMP] ===== {} Side =====", common::toString(side)));

    for (int layer = 0; layer < kSlotsPerSide; ++layer) {
      const auto& slot = side_book.slots[layer];
      const auto tick = side_book.layer_ticks[layer];

      if (slot.state == OMOrderState::kInvalid || slot.state == OMOrderState::kDead) {
        continue;
      }

      logger_.debug(std::format(
        "[SLOT_DUMP]   Layer[{}]: state={}, tick={}, price={:.2f}, qty={:.6f}, oid={}",
        layer, trading::toString(slot.state), tick,
        slot.price.value, slot.qty.value, slot.cl_order_id.value));
    }
  }

  logger_.info(std::format("[SLOT_DUMP] ========== END {} ==========", context));
}
}  // namespace trading

#endif  // ORDER_MANAGER_TPP