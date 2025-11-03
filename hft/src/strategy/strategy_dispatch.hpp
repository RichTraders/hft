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

#ifndef STRATEGY_DISPATCH_HPP
#define STRATEGY_DISPATCH_HPP

#include "logger.h"
#include "types.h"

struct MarketData;

namespace trading {
class OrderManager;
class FeatureEngine;
class MarketOrderBook;
struct ExecutionReport;

struct StrategyContext {
  OrderManager* order_manager;
  const FeatureEngine* feature_engine;
  common::Logger::Producer logger;
  void* strategy_data;

  StrategyContext(OrderManager* order_manager,
                  const FeatureEngine* feature_engine, common::Logger* logger,
                  void* data)
      : order_manager(order_manager),
        feature_engine(feature_engine),
        logger(logger->make_producer()),
        strategy_data(data) {}
};

using OnOrderbookUpdatedFn = void (*)(StrategyContext&, const common::TickerId&,
                                      common::Price, common::Side,
                                      const MarketOrderBook*);
using OnTradeUpdatedFn = void (*)(StrategyContext&, const MarketData*,
                                  MarketOrderBook*);
using OnOrderUpdatedFn = void (*)(StrategyContext&, const ExecutionReport*);
using CreateStrategyDataFn = void* (*)(OrderManager*, const FeatureEngine*,
                                       common::Logger*,
                                       const common::TradeEngineCfgHashMap&);
using DestroyStrategyDataFn = void (*)(void*);

struct StrategyVTable {
  OnOrderbookUpdatedFn on_orderbook_updated;
  OnTradeUpdatedFn on_trade_updated;
  OnOrderUpdatedFn on_order_updated;
  CreateStrategyDataFn create_data;
  DestroyStrategyDataFn destroy_data;
};

class StrategyDispatch {
 public:
  static StrategyDispatch& instance() {
    static StrategyDispatch dispatch;
    return dispatch;
  }

  void register_strategy(const std::string& name,
                         const StrategyVTable& vtable) {
    vtables_[name] = vtable;
  }

  const StrategyVTable* get_vtable(const std::string& name) const {
    auto strategy = vtables_.find(name);
    return (strategy != vtables_.end()) ? &strategy->second : nullptr;
  }

  std::vector<std::string> get_strategy_names() const {
    std::vector<std::string> names;
    names.reserve(vtables_.size());
    for (const auto& [name, _] : vtables_) {
      names.push_back(name);
    }
    return names;
  }

 private:
  StrategyDispatch() = default;
  std::unordered_map<std::string, StrategyVTable> vtables_;
};

template <class T>
struct Registrar {
  explicit Registrar(const char* name) {
    const StrategyVTable vtable{
        .on_orderbook_updated =
            +[](StrategyContext& ctx, const common::TickerId& tid,
                common::Price price, common::Side side,
                const MarketOrderBook* book) {
              static_cast<T*>(ctx.strategy_data)
                  ->on_orderbook_updated(tid, price, side, book);
            },
        .on_trade_updated =
            +[](StrategyContext& ctx, const MarketData* market_data,
                MarketOrderBook* book) {
              static_cast<T*>(ctx.strategy_data)
                  ->on_trade_updated(market_data, book);
            },
        .on_order_updated =
            +[](StrategyContext& ctx, const ExecutionReport* report) {
              static_cast<T*>(ctx.strategy_data)->on_order_updated(report);
            },
        .create_data =
            +[](OrderManager* order_manager,
                const FeatureEngine* feature_engine, common::Logger* logger,
                const common::TradeEngineCfgHashMap& cfg) -> void* {
          return new T(order_manager, feature_engine, logger, cfg);
        },
        .destroy_data = +[](void* pointer) { delete static_cast<T*>(pointer); },
    };
    StrategyDispatch::instance().register_strategy(name, vtable);
  }
};
}  // namespace trading

#endif  // STRATEGY_DISPATCH_HPP
