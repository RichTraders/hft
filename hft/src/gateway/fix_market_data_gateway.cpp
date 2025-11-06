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

#include "fix_market_data_gateway.h"

#include <format>

#include "core/NewOroFix44/market_data.h"
#include "src/trade_engine.h"

namespace trading {

FixMarketDataGateway::FixMarketDataGateway(
    const std::string& sender_comp_id, const std::string& target_comp_id,
    common::Logger* logger, TradeEngine* trade_engine,
    common::MemoryPool<MarketUpdateData>* market_update_data_pool,
    common::MemoryPool<MarketData>* market_data_pool)
    : market_update_data_pool_(market_update_data_pool),
      market_data_pool_(market_data_pool),
      logger_(logger->make_producer()),
      trade_engine_(trade_engine),
      app_(std::make_unique<core::FixMarketDataApp>(sender_comp_id, target_comp_id,
                                                     logger, market_data_pool_)) {
  // Register FIX message callbacks
  app_->register_callback(
      "A", [this](auto&& msg) { on_login(std::forward<decltype(msg)>(msg)); });
  app_->register_callback(
      "W", [this](auto&& msg) { on_snapshot(std::forward<decltype(msg)>(msg)); });
  app_->register_callback(
      "X", [this](auto&& msg) { on_subscribe(std::forward<decltype(msg)>(msg)); });
  app_->register_callback(
      "1", [this](auto&& msg) { on_heartbeat(std::forward<decltype(msg)>(msg)); });
  app_->register_callback(
      "y", [this](auto&& msg) { on_instrument_list(std::forward<decltype(msg)>(msg)); });
  app_->register_callback(
      "3", [this](auto&& msg) { on_reject(std::forward<decltype(msg)>(msg)); });
  app_->register_callback(
      "5", [this](auto&& msg) { on_logout(std::forward<decltype(msg)>(msg)); });

  app_->start();
  logger_.info("[Constructor] FixMarketDataGateway Created");
}

FixMarketDataGateway::~FixMarketDataGateway() {
  logger_.info("[Destructor] FixMarketDataGateway Destroy");
}

void FixMarketDataGateway::stop() {
  app_->stop();
}

void FixMarketDataGateway::subscribe_market_data(const std::string& req_id,
                                                  const std::string& depth,
                                                  const std::string& symbol,
                                                  bool subscribe) {
  // Store subscription parameters for resubscribe
  req_id_ = req_id;
  depth_ = depth;
  symbol_ = symbol;

  const std::string message =
      app_->create_market_data_subscription_message(req_id, depth, symbol, subscribe);

  if (UNLIKELY(!app_->send(message))) {
    logger_.error("[Message] failed to send market data subscription");
  }
}

void FixMarketDataGateway::request_instrument_list(const std::string& symbol) {
  const std::string instrument_message = app_->request_instrument_list_message(symbol);
  if (UNLIKELY(!app_->send(instrument_message))) {
    logger_.error("[Message] failed to send instrument list request");
  }
}

void FixMarketDataGateway::on_login(FIX8::Message*) {
  logger_.info("[Login] Market data gateway login successful");
}

void FixMarketDataGateway::on_snapshot(FIX8::Message* msg) {
  logger_.info("Snapshot made");

  auto* snapshot_data =
      market_update_data_pool_->allocate(app_->create_snapshot_data_message(msg));

  if (UNLIKELY(snapshot_data == nullptr)) {
    logger_.error("[Message] failed to create snapshot");
    resubscribe();

    for (auto& market_data : snapshot_data->data) {
      market_data_pool_->deallocate(market_data);
    }
    market_update_data_pool_->deallocate(snapshot_data);
    return;
  }

  state_ = StreamState::kApplyingSnapshot;
  update_index_ = snapshot_data->end_idx;

  if (UNLIKELY(!trade_engine_->on_market_data_updated(snapshot_data))) {
    logger_.error("[Message] failed to send snapshot");
  }

  state_ = StreamState::kRunning;
}

void FixMarketDataGateway::on_subscribe(FIX8::Message* msg) {
  auto* data =
      market_update_data_pool_->allocate(app_->create_market_data_message(msg));

  if (UNLIKELY(data == nullptr)) {
    logger_.error("[Error] Failed to allocate market data message");
#ifdef NDEBUG
    app_->stop();
    exit(1);
#endif
    return;
  }

  if (UNLIKELY(state_ == StreamState::kAwaitingSnapshot)) {
    logger_.info("Waiting for snapshot");
    return;
  }

  if (UNLIKELY((data->type == kNone) ||
               (data->type == kMarket && data->start_idx != update_index_ + 1 &&
                update_index_ != 0ULL))) {
    logger_.error(std::format(
        "Update index is outdated. current index: {}, new index: {}",
        update_index_, data->start_idx));

    resubscribe();

    for (const auto& market_data : data->data) {
      market_data_pool_->deallocate(market_data);
    }
    market_update_data_pool_->deallocate(data);
    return;
  }

  update_index_ = data->end_idx;
  if (UNLIKELY(!trade_engine_->on_market_data_updated(data))) {
    logger_.error("[Message] failed to send market data update");
  }
}

void FixMarketDataGateway::on_reject(FIX8::Message* msg) {
  const auto rejected_message = app_->create_reject_message(msg);
  logger_.error(std::format("[Message] {}", rejected_message.toString()));
  if (rejected_message.session_reject_reason == "A") {
    app_->stop();
  }
}

void FixMarketDataGateway::on_logout(FIX8::Message*) {
  logger_.info("[Message] logout");
}

void FixMarketDataGateway::on_instrument_list(FIX8::Message* msg) {
  logger_.info("[Message] on_instrument_list");
  const InstrumentInfo instrument_message = app_->create_instrument_list_message(msg);
  logger_.info(std::format("[Message]: {}", instrument_message.toString()));
}

void FixMarketDataGateway::on_heartbeat(FIX8::Message* msg) {
  auto message = app_->create_heartbeat_message(msg);
  if (UNLIKELY(!app_->send(message))) {
    logger_.error("[Message] failed to send heartbeat");
  }
}

void FixMarketDataGateway::resubscribe() {
  logger_.info("Try resubscribing");
  current_generation_.store(generation_.fetch_add(1, std::memory_order_acq_rel) + 1,
                            std::memory_order_release);

  // Unsubscribe
  const std::string msg_unsub = app_->create_market_data_subscription_message(
      req_id_, depth_, symbol_, /*subscribe=*/false);
  app_->send(msg_unsub);

  // Resubscribe
  const std::string msg_sub = app_->create_market_data_subscription_message(
      req_id_, depth_, symbol_, /*subscribe=*/true);
  app_->send(msg_sub);

  ++generation_;
  state_ = StreamState::kAwaitingSnapshot;
  update_index_ = 0ULL;
}

}  // namespace trading
