//
// Created by neworo2 on 25. 7. 28.
//

#include "order_gateway.h"
#include "fix_oe_app.h"
#include "trade_engine.h"

constexpr int kPort = 9000;

namespace trading {

OrderGateway::OrderGateway(common::Logger* logger, TradeEngine* trade_engine,
                           common::MemoryPool<OrderData>* order_data_pool)
    : logger_(logger),
      trade_engine_(trade_engine),
      order_data_pool_(order_data_pool),
#ifdef DEBUG
      app_(std::make_unique<core::FixOrderEntryAppApp>(
          "fix-oe.testnet.binance.vision",
#else
      app_(std::make_unique<core::FixOrderEntryApp>(
          "fix-oe.binance.com",
#endif
          kPort, "BMDWATCH", "SPOT", logger_, nullptr)) {

  // app_->register_callback(
  //     "A", [this](auto&& msg) { on_login(std::forward<decltype(msg)>(msg)); });
  // app_->register_callback("W", [this](auto&& msg) {
  //   on_snapshot(std::forward<decltype(msg)>(msg));
  // });
  // app_->register_callback("X", [this](auto&& msg) {
  //   on_subscribe(std::forward<decltype(msg)>(msg));
  // });
  // app_->register_callback("1", [this](auto&& msg) {
  //   on_heartbeat(std::forward<decltype(msg)>(msg));
  // });
  // app_->register_callback(
  //     "3", [this](auto&& msg) { on_reject(std::forward<decltype(msg)>(msg)); });
  // app_->register_callback(
  //     "5", [this](auto&& msg) { on_logout(std::forward<decltype(msg)>(msg)); });

  app_->start();
}

OrderGateway::~OrderGateway() {
  app_->stop();
}

void OrderGateway::on_login(FIX8::Message*) {
  logger_->info("login successful");
  logger_->info("sent order message");
}

void OrderGateway::on_execution_report(FIX8::Message* msg) {
  (void)msg;
  logger_->info("on_execution_report");
}

void OrderGateway::on_order_cancel_reject(FIX8::Message* msg) {
  (void)msg;
  logger_->info("on_order_cancel_reject");
}

void OrderGateway::on_order_mass_cancel_report(FIX8::Message* msg) {
  (void)msg;
  logger_->info("on_order_mass_cancel_report");
  MarketUpdateData tmp;
  trade_engine_->on_market_data_updated(&tmp);

  const OrderData order_data;
  order_data_pool_->allocate(order_data);
}

void OrderGateway::on_order_mass_status_response(FIX8::Message* msg) {
  (void)msg;
  logger_->info("on_order_mass_status_response");
}

void OrderGateway::on_logout(FIX8::Message*) {
  logger_->info("logout");
}

void OrderGateway::on_heartbeat(FIX8::Message* msg) {
  auto message = app_->create_heartbeat_message(msg);
  app_->send(message);
}
}  // namespace trading