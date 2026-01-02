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

#ifndef ORDER_MANAGER_HPP
#define ORDER_MANAGER_HPP

#include <format>
#include <optional>
#include <string>
#include <vector>

#include "fast_clock.h"
#include "fixed_point_config.hpp"
#include "ini_config.hpp"
#include "layer_book.h"
#include "logger.h"
#include "market_data.h"
#include "oe_traits_config.hpp"
#include "order_entry.h"
#include "order_expiry_manager.h"
#include "order_state_manager.h"
#include "orders.h"
#include "performance.h"
#include "quote_reconciler.h"
#include "reserved_position_tracker.h"
#include "risk_manager.h"

namespace trading {
template <typename Strategy>
class TradeEngine;

using common::OrderId;
using common::Price;
using common::Qty;
using common::Side;
using common::TickerId;
using order::LayerBook;
using order::PendingReplaceInfo;

template <typename Strategy>
class OrderManager {
 public:
  using QuoteIntentType = typename Strategy::QuoteIntentType;

  static constexpr bool kSupportsCancelAndReorder =
      SelectedOeTraits::supports_cancel_and_reorder();
  static constexpr bool kSupportsPositionSide =
      SelectedOeTraits::supports_position_side();

  OrderManager(const common::Logger::Producer& logger,
      TradeEngine<Strategy>* trade_engine, RiskManager& risk_manager)
      : layer_book_(INI_CONFIG.get("meta", "ticker")),
        trade_engine_(trade_engine),
        risk_manager_(risk_manager),
        logger_(logger),
        fast_clock_(INI_CONFIG.get_double("cpu_info", "clock"),
            INI_CONFIG.get_int("cpu_info", "interval")),
        ticker_size_(INI_CONFIG.get_double("meta", "ticker_size")),
        reconciler_(ticker_size_),
        tick_converter_(ticker_size_),
        state_manager_(logger_, tick_converter_),
        expiry_manager_(INI_CONFIG.get_double("orders", "ttl_reserved_ns"),
            INI_CONFIG.get_double("orders", "ttl_live_ns")) {
    logger_.info("[Constructor] OrderManager Created");
  }

  // NOLINTNEXTLINE(modernize-use-equals-default) - logs destruction
  ~OrderManager() { logger_.info("[Destructor] OrderManager Destroy"); }

  void on_order_updated(const ExecutionReport* response) noexcept {
    auto& side_book = layer_book_.side_book(response->symbol,
        response->side,
        response->position_side);
    const auto now = fast_clock_.get_timestamp();

    state_manager_.handle_execution_report(response,
        side_book,
        position_tracker_,
        now);

    if (response->ord_status == OrdStatus::kNew) {
      int layer = LayerBook::find_layer_by_id(side_book, response->cl_order_id);
      if (layer < 0) {
        const auto iter =
            side_book.new_id_to_layer.find(response->cl_order_id.value);
        if (iter != side_book.new_id_to_layer.end())
          layer = iter->second;
      }
      if (layer >= 0) {
        expiry_manager_.register_expiry(response->symbol,
            response->side,
            response->position_side,
            layer,
            response->cl_order_id,
            OMOrderState::kLive,
            now);
      }
    } else if (response->ord_status == OrdStatus::kPartiallyFilled) {
      const int layer = LayerBook::find_layer_by_id(side_book, response->cl_order_id);
      if (layer >= 0 && side_book.slots[layer].state == OMOrderState::kLive) {
        expiry_manager_.register_expiry(response->symbol,
            response->side,
            response->position_side,
            layer,
            response->cl_order_id,
            OMOrderState::kLive,
            now);
      }
    }

    logger_.debug("[OrderUpdated]Order Id:{} reserved_position:{}",
        response->cl_order_id.value,
        position_tracker_.get_reserved());

    dump_all_slots(response->symbol,
        std::format("After {} oid={}",
            toString(response->ord_status),
            response->cl_order_id.value));
  }

  void on_instrument_info(const InstrumentInfo& instrument_info) noexcept {
    if (!instrument_info.symbols.empty()) {
      const std::string target_ticker = INI_CONFIG.get("meta", "ticker");
      auto sym = std::find_if(instrument_info.symbols.begin(),
          instrument_info.symbols.end(),
          [&target_ticker](
              auto symbol) { return symbol.symbol == target_ticker; });

      if (sym != instrument_info.symbols.end()) {
        venue_policy_.set_qty_increment(
            static_cast<int64_t>(sym->min_qty_increment * common::FixedPointConfig::kQtyScale));
      }

      logger_.info("[OrderManager] Updated qty_increment to {}",
          instrument_info.symbols[0].min_qty_increment);
    }
  }

  void new_order(const TickerId& ticker_id, common::PriceType price, Side side, common::QtyType qty,
      OrderId order_id,
      std::optional<common::PositionSide> position_side =
          std::nullopt) noexcept {
    const RequestCommon new_request{
        .req_type = ReqeustType::kNewSingleOrderData,
        .cl_order_id = order_id,
        .symbol = ticker_id,
        .side = side,
        .order_qty = qty,
        .ord_type = OrderType::kLimit,
        .price = price,
        .time_in_force = TimeInForce::kGoodTillCancel,
        .position_side = position_side};
    trade_engine_->send_request(new_request);

    logger_.info("[OrderRequest]Sent new order {}", new_request.toString());
  }

  void modify_order(const TickerId& ticker_id,
      const OrderId& cancel_new_order_id, const OrderId& order_id,
      const OrderId& original_order_id, common::PriceType price, Side side, common::QtyType qty,
      std::optional<common::PositionSide> position_side =
          std::nullopt) noexcept {

    if constexpr (kSupportsCancelAndReorder) {
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
          .time_in_force = TimeInForce::kGoodTillCancel,
          .position_side = position_side};
      trade_engine_->send_request(new_request);
      logger_.info("[OrderRequest]Sent cancel-and-reorder {}",
          new_request.toString());
    } else {
      const RequestCommon modify_request{.req_type = ReqeustType::kOrderModify,
          .cl_order_id = order_id,
          .orig_cl_order_id = original_order_id,
          .symbol = ticker_id,
          .side = side,
          .order_qty = qty,
          .ord_type = OrderType::kLimit,
          .price = price,
          .time_in_force = TimeInForce::kGoodTillCancel,
          .position_side = position_side};
      trade_engine_->send_request(modify_request);
      logger_.info("[OrderRequest]Sent modify order {}",
          modify_request.toString());
    }
  }

  void cancel_order(const TickerId& ticker_id, const OrderId& original_order_id,
      std::optional<common::PositionSide> position_side =
          std::nullopt) noexcept {
    const RequestCommon cancel_request{
        .req_type = ReqeustType::kOrderCancelRequest,
        .cl_order_id = original_order_id,
        .orig_cl_order_id = original_order_id,
        .symbol = ticker_id,
        .position_side = position_side};
    trade_engine_->send_request(cancel_request);

    logger_.info("[OrderRequest]Sent cancel {}", cancel_request.toString());
  }

  void apply(const std::vector<QuoteIntentType>& intents) noexcept {
    START_MEASURE(Trading_OrderManager_apply);

    const auto now = fast_clock_.get_timestamp();

    if (intents.empty()) {
      sweep_expired_orders(now);
      END_MEASURE(Trading_OrderManager_apply, logger_);
      return;
    }

    auto actions = reconciler_.diff(intents, layer_book_, fast_clock_);
    const auto& ticker = intents.front().ticker;

    venue_policy_.filter_by_venue(ticker, actions, now, layer_book_);
    filter_by_risk(intents, actions);

    process_new_orders(ticker, actions, now);
    process_replace_orders(ticker, actions, now);
    process_cancel_orders(ticker, actions, now);
    sweep_expired_orders(now);

    END_MEASURE(Trading_OrderManager_apply, logger_);
  }

  OrderManager() = delete;
  OrderManager(const OrderManager&) = delete;
  OrderManager(const OrderManager&&) = delete;
  OrderManager& operator=(const OrderManager&) = delete;
  OrderManager& operator=(const OrderManager&&) = delete;

 private:
  order::LayerBook layer_book_;
  TradeEngine<Strategy>* trade_engine_ = nullptr;
  RiskManager& risk_manager_;
  const common::Logger::Producer& logger_;
  common::FastClock fast_clock_;
  const double ticker_size_ = 0;
  order::QuoteReconciler<QuoteIntentType> reconciler_;
  order::VenuePolicy venue_policy_;
  order::TickConverter tick_converter_;

  OrderStateManager state_manager_;
  ReservedPositionTracker position_tracker_;
  OrderExpiryManager expiry_manager_;

  void filter_by_risk(const std::vector<QuoteIntentType>& intents,
      order::Actions& acts) {
    const auto& ticker =
        intents.empty() ? std::string{} : intents.front().ticker;
    common::QtyType running = common::QtyType::from_raw(position_tracker_.get_reserved());
    auto allow_new = [&](auto& actions) {
      for (auto action = actions.begin(); action != actions.end();) {
        const common::QtyType delta = action->qty;
        if (risk_manager_.check_pre_trade_risk(ticker,
                action->side,
                delta,
                running) == RiskCheckResult::kAllowed) {
          running = common::QtyType::from_raw(
              running.value + common::sideToValue(action->side) * delta.value);
          ++action;
        } else {
          action = actions.erase(action);
        }
      }
    };
    auto allow_repl = [&](auto& actions) {
      for (auto action = actions.begin(); action != actions.end();) {
        const common::QtyType delta = common::QtyType::from_raw(
            action->qty.value - action->last_qty.value);
        if (risk_manager_.check_pre_trade_risk(ticker,
                action->side,
                delta,
                running) == RiskCheckResult::kAllowed) {
          running = common::QtyType::from_raw(
              running.value + common::sideToValue(action->side) * delta.value);
          ++action;
        } else {
          action = actions.erase(action);
        }
      }
    };

    allow_new(acts.news);
    allow_repl(acts.repls);
  }

  void process_new_orders(const common::TickerId& ticker,
      order::Actions& actions, uint64_t now) noexcept {
    for (auto& action : actions.news) {
      auto& side_book =
          layer_book_.side_book(ticker, action.side, action.position_side);
      auto& [state, price, qty, last_used, cl_order_id] =
          side_book.slots[action.layer];

      const uint64_t tick = tick_converter_.to_ticks_raw(action.price.value);
      if (const int existing = LayerBook::find_layer_by_ticks(side_book, tick);
          existing >= 0 && existing != action.layer) {
        continue;
      }

      side_book.layer_ticks[action.layer] = tick;
      price = action.price;
      qty = action.qty;
      cl_order_id = action.cl_order_id;
      state = OMOrderState::kReserved;
      last_used = now;

      new_order(ticker,
          action.price,
          action.side,
          action.qty,
          action.cl_order_id,
          action.position_side);
      position_tracker_.add_reserved(action.side, action.qty.value);

      logger_.info(
          "[Apply][NEW] tick:{}/ layer={}, side:{}, order_id={}, "
          "reserved_position_={}",
          tick,
          action.layer,
          common::toString(action.side),
          common::toString(action.cl_order_id),
          position_tracker_.get_reserved());

      expiry_manager_.register_expiry(ticker,
          action.side,
          action.position_side,
          action.layer,
          action.cl_order_id,
          OMOrderState::kReserved,
          now);
    }
  }

  void process_replace_orders(const common::TickerId& ticker,
      order::Actions& actions, uint64_t now) noexcept {
    for (auto& action : actions.repls) {
      auto& side_book =
          layer_book_.side_book(ticker, action.side, action.position_side);
      auto& slot = side_book.slots[action.layer];

      const uint64_t tick = tick_converter_.to_ticks_raw(action.price.value);
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
      slot.last_used = now;

      for (auto iter = side_book.new_id_to_layer.begin();
           iter != side_book.new_id_to_layer.end();) {
        if (iter->second == action.layer) {
          const auto to_erase = iter;
          ++iter;
          side_book.new_id_to_layer.erase(to_erase);
        } else
          ++iter;
      }

      if constexpr (kSupportsCancelAndReorder) {
        const auto cancel_new_order_id = OrderId{action.cl_order_id.value - 1};

        side_book.orig_id_to_layer[cancel_new_order_id.value] = action.layer;
        side_book.new_id_to_layer[action.cl_order_id.value] = action.layer;
        side_book.pending_repl[action.layer] = PendingReplaceInfo{action.price,
            action.qty,
            tick,
            action.cl_order_id,
            action.last_qty,
            action.original_cl_order_id,
            original_price,
            original_tick};
        modify_order(ticker,
            cancel_new_order_id,
            action.cl_order_id,
            action.original_cl_order_id,
            action.price,
            action.side,
            action.qty,
            action.position_side);
      } else {
        side_book.new_id_to_layer[action.original_cl_order_id.value] =
            action.layer;
        side_book.pending_repl[action.layer] = PendingReplaceInfo{action.price,
            action.qty,
            tick,
            action.original_cl_order_id,
            action.last_qty,
            action.original_cl_order_id,
            original_price,
            original_tick};
        modify_order(ticker,
            action.original_cl_order_id,
            action.original_cl_order_id,
            action.original_cl_order_id,
            action.price,
            action.side,
            action.qty,
            action.position_side);
      }

      const auto delta_qty = action.qty.value - action.last_qty.value;
      position_tracker_.add_reserved(action.side, delta_qty);
      logger_.info(
          "[Apply][REPLACE] tick:{}/ layer={}, side:{}, order_id={}, "
          "reserved_position_={}",
          tick,
          action.layer,
          common::toString(action.side),
          common::toString(action.cl_order_id),
          position_tracker_.get_reserved());
      expiry_manager_.register_expiry(ticker,
          action.side,
          action.position_side,
          action.layer,
          action.cl_order_id,
          OMOrderState::kCancelReserved,
          now);
    }
  }

  void process_cancel_orders(const common::TickerId& ticker,
      order::Actions& actions, uint64_t now) noexcept {
    for (auto& action : actions.cancels) {
      auto& side_book =
          layer_book_.side_book(ticker, action.side, action.position_side);
      auto& slot = side_book.slots[action.layer];
      slot.state = OMOrderState::kCancelReserved;
      slot.last_used = now;
      cancel_order(ticker, action.original_cl_order_id, action.position_side);
      logger_.info(
          "[Apply][CANCEL] layer={}, side:{}, order_id={}, "
          "reserved: {}",
          action.layer,
          common::toString(action.side),
          common::toString(action.original_cl_order_id),
          position_tracker_.get_reserved());
    }
  }

  void sweep_expired_orders(uint64_t now) noexcept {
    auto expired = expiry_manager_.sweep_expired(now);
    for (const auto& key : expired) {
      auto& side_book =
          layer_book_.side_book(key.symbol, key.side, key.position_side);
      if (UNLIKELY(key.layer >= side_book.slots.size()))
        continue;

      auto& slot = side_book.slots[key.layer];
      if (slot.cl_order_id != key.cl_order_id)
        continue;

      if (slot.state == OMOrderState::kDead ||
          slot.state == OMOrderState::kCancelReserved)
        continue;

      if (slot.state == OMOrderState::kLive ||
          slot.state == OMOrderState::kReserved) {
        slot.state = OMOrderState::kCancelReserved;
        slot.last_used = now;

        cancel_order(key.symbol, slot.cl_order_id);

        logger_.info(
            "[TTL] Cancel sent (state={}, layer={}, oid={}, "
            "remaining_ns={})",
            trading::toString(slot.state),
            key.layer,
            common::toString(slot.cl_order_id),
            key.expire_ts - now);
      }
    }
  }

  [[nodiscard]] OrderId gen_order_id() noexcept {
    const auto now = fast_clock_.get_timestamp();
    return OrderId{now};
  }

  void dump_all_slots(const std::string& symbol,
      std::string_view context) noexcept {
    logger_.debug("[SLOT_DUMP] ========== {} ==========", context);
    logger_.debug("[SLOT_DUMP] Symbol: {}, Reserved: {}",
        symbol,
        position_tracker_.get_reserved());

    for (int side_idx = 0; side_idx < 2; ++side_idx) {
      const auto side =
          side_idx == 0 ? common::Side::kBuy : common::Side::kSell;
      const auto& side_book = layer_book_.side_book(symbol, side);

      logger_.debug("[SLOT_DUMP] ===== {} Side =====", common::toString(side));

      for (int layer = 0; layer < kSlotsPerSide; ++layer) {
        const auto& slot = side_book.slots[layer];
        const auto tick = side_book.layer_ticks[layer];

        if (slot.state == OMOrderState::kInvalid ||
            slot.state == OMOrderState::kDead) {
          continue;
        }

        logger_.debug(
            "[SLOT_DUMP]   Layer[{}]: state={}, tick={}, "
            "price={:.2f}, qty={:.6f}, oid={}",
            layer,
            trading::toString(slot.state),
            tick,
            slot.price.to_double(),
            slot.qty.to_double(),
            slot.cl_order_id.value);
      }
    }

    logger_.debug("[SLOT_DUMP] ========== END {} ==========", context);
  }
};
}  // namespace trading

#endif  // ORDER_MANAGER_HPP
