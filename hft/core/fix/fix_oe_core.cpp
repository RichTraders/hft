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

#include <string>

#include <fix8/f8includes.hpp>

#include "fix_oe_core.h"
#include "NewOroFix44OE_types.hpp"
#include "NewOroFix44OE_router.hpp"
#include "NewOroFix44OE_classes.hpp"
#include "authorization.h"
#include "ini_config.hpp"
#include "performance.h"
#include "response_manager.h"

namespace core {
using namespace FIX8::NewOroFix44OE;

FixOeCore::FixOeCore(SendId sender_comp_id, TargetId target_comp_id,
                     const common::Logger::Producer& logger,
                     trading::ResponseManager* response_manager)
    : sender_comp_id_(std::move(sender_comp_id)),
      target_comp_id_(std::move(target_comp_id)),
      logger_(logger),
      response_manager_(response_manager),
      qty_precision_(INI_CONFIG.get_int("meta", "qty_precision")),
      price_precision_(INI_CONFIG.get_int("meta", "price_precision")) {
  logger_.info("[Constructor] FixOeCore Created");
}

FixOeCore::~FixOeCore() {
  logger_.info("[Destructor] FixOeCore Destroy");
}

std::string FixOeCore::create_log_on_message(const std::string& sig_b64,
                                             const std::string& timestamp) {
  FIX8::NewOroFix44OE_ctx();  // 왜하는겨?
  Logon request;

  FIX8::MessageBase* header = request.Header();
  *header << new SenderCompID(sender_comp_id_)
          << new TargetCompID(target_comp_id_) << new MsgSeqNum(sequence_++)
          << new SendingTime(timestamp);

  request << new EncryptMethod(EncryptMethod_NONE) << new HeartBtInt(30)
          << new ResetSeqNumFlag(true) << new ResponseMode(1)
          << new DropCopyFlag(false)
          << new RawDataLength(static_cast<int>(sig_b64.size()))
          << new RawData(sig_b64) << new Username(AUTHORIZATION.get_api_key())
          << new MessageHandling(2);

  if (auto* scid = static_cast<MsgType*>(request.Header()->get_field(35)))
    scid->set("A");

  std::string wire;
  request.encode(wire);
  return wire;
}

std::string FixOeCore::create_log_out_message() {
  Logout request;
  request.Header()->add_field(new SenderCompID(sender_comp_id_));
  request.Header()->add_field(new TargetCompID(target_comp_id_));
  request.Header()->add_field(new MsgSeqNum(sequence_++));
  request.Header()->add_field(new SendingTime());
  if (auto* scid = static_cast<MsgType*>(request.Header()->get_field(35)))
    scid->set("5");

  std::string wire;
  request.encode(wire);
  return wire;
}

std::string FixOeCore::create_heartbeat_message(FIX8::Message* message) {
  auto test_req_id = message->get<TestReqID>();

  Heartbeat request;
  request.Header()->add_field(new SenderCompID(sender_comp_id_));
  request.Header()->add_field(new TargetCompID(target_comp_id_));
  request.Header()->add_field(new MsgSeqNum(sequence_++));
  request.Header()->add_field(new SendingTime());
  request << new TestReqID(*test_req_id);

  if (auto* scid = static_cast<MsgType*>(request.Header()->get_field(35)))
    scid->set("0");

  std::string wire;
  request.encode(wire);
  return wire;
}

std::string FixOeCore::create_order_message(
    const trading::NewSingleOrderData& order_data) {
  NewOrderSingle request;

  FIX8::MessageBase* header = request.Header();
  *header << new SenderCompID(sender_comp_id_)
          << new TargetCompID(target_comp_id_) << new MsgSeqNum(sequence_++)
          << new SendingTime();

  request.add_field(new ClOrdID(std::to_string(order_data.cl_order_id.value)));
  request.add_field(new Symbol(order_data.symbol));
  request.add_field(new Side(trading::to_char(order_data.side)));
  request.add_field(new OrdType(trading::to_char(order_data.ord_type)));
  request.add_field(new OrderQty(order_data.order_qty.value));
  request.add_field(new SelfTradePreventionMode(
      trading::to_char(order_data.self_trade_prevention_mode)));

  if (order_data.ord_type == trading::OrderType::kLimit) {
    // Limit 주문일 때만
    request.add_field(new Price(order_data.price.value));
    request.add_field(
        new TimeInForce(trading::to_char(order_data.time_in_force)));
  }
  static_cast<OrderQty*>(request.get_field(38))->set_precision(qty_precision_);
  //Qty
  static_cast<OrderQty*>(request.get_field(44))
      ->set_precision(price_precision_);  //Price

  /*
   * SelfTradePreventionMode
   * StrategyID
   * CashOrderQty
   * MaxFloor
   * TriggeringInstruction
   * ExecInst
   */

  std::string wire;
  request.encode(wire);
  return wire;
}

std::string FixOeCore::create_cancel_order_message(
    const trading::OrderCancelRequest& cancel_request) {
  OrderCancelRequest request;

  FIX8::MessageBase* header = request.Header();
  *header << new SenderCompID(sender_comp_id_)
          << new TargetCompID(target_comp_id_) << new MsgSeqNum(sequence_++)
          << new SendingTime();

  request.add_field(
      new ClOrdID(std::to_string(cancel_request.cl_order_id.value)));
  request.add_field(
      new OrigClOrdID(std::to_string(cancel_request.orig_cl_order_id.value)));
  request.add_field(new Symbol(cancel_request.symbol));

  std::string wire;
  request.encode(wire);
  return wire;
}

std::string FixOeCore::create_cancel_and_reorder_message(
    const trading::OrderCancelAndNewOrderSingle& cancel_and_re_order) {
  OrderCancelRequestAndNewOrderSingle request;

  FIX8::MessageBase* header = request.Header();
  *header << new SenderCompID(sender_comp_id_)
          << new TargetCompID(target_comp_id_) << new MsgSeqNum(sequence_++)
          << new SendingTime();

  request.add_field(new OrigClOrdID(
      std::to_string(cancel_and_re_order.cl_origin_order_id.value)));
  request.add_field(new CancelClOrdID(
      std::to_string(cancel_and_re_order.cancel_new_order_id.value)));
  request.add_field(
      new ClOrdID(std::to_string(cancel_and_re_order.cl_new_order_id.value)));
  request.add_field(new Symbol(cancel_and_re_order.symbol));
  request.add_field(new Side(trading::to_char(cancel_and_re_order.side)));
  request.add_field(
      new OrdType(trading::to_char(cancel_and_re_order.ord_type)));
  request.add_field(new OrderQty(cancel_and_re_order.order_qty.value));
  request.add_field(new SelfTradePreventionMode(
      trading::to_char(cancel_and_re_order.self_trade_prevention_mode)));
  request.add_field(new OrderCancelRequestAndNewOrderSingleMode(
      cancel_and_re_order.order_cancel_request_and_new_order_single_mode));

  if (cancel_and_re_order.ord_type == trading::OrderType::kLimit) {
    // Limit 주문일 때만
    request.add_field(new Price(cancel_and_re_order.price.value));
    request.add_field(
        new TimeInForce(trading::to_char(cancel_and_re_order.time_in_force)));
    static_cast<OrderQty*>(request.get_field(44))
      ->set_precision(price_precision_);  //Price
  }
  static_cast<OrderQty*>(request.get_field(38))->set_precision(qty_precision_);
  //Qty


  std::string wire;
  request.encode(wire);
  return wire;
}

std::string FixOeCore::create_order_all_cancel(
    const trading::OrderMassCancelRequest& all_order_cancel) {
  OrderMassCancelRequest request;

  FIX8::MessageBase* header = request.Header();
  *header << new SenderCompID(sender_comp_id_)
          << new TargetCompID(target_comp_id_) << new MsgSeqNum(sequence_++)
          << new SendingTime();

  request.add_field(
      new ClOrdID(std::to_string(all_order_cancel.cl_order_id.value)));
  request.add_field(new Symbol(all_order_cancel.symbol));
  request.add_field(
      new MassCancelRequestType(all_order_cancel.mass_cancel_request_type));

  std::string wire;
  request.encode(wire);
  return wire;
}

trading::ExecutionReport* FixOeCore::create_execution_report_message(
    WireExecutionReport msg) {
  const auto* cl_order_id = msg->get<ClOrdID>();
  const auto* symbol = msg->get<Symbol>();
  const auto* exec_type = msg->get<ExecType>();
  const auto* ord_status = msg->get<OrdStatus>();
  const auto* cum_qty = msg->get<CumQty>();
  const auto* leaves_qty = msg->get<LeavesQty>();
  const auto* last_qty = msg->get<LastQty>();
  const auto* price = msg->get<Price>();
  const auto* error_code = msg->get<ErrorCode>();
  const auto* text = msg->get<Text>();
  const auto* side = msg->get<Side>();

  auto* ret = response_manager_->execution_report_allocate();

  ret->symbol = symbol->get();
  ret->cl_order_id = common::OrderId{std::stoull(cl_order_id->get())};
  ret->cum_qty.value = cum_qty->get();
  ret->exec_type = trading::exec_type_from_char(exec_type->get());
  ret->last_qty.value = last_qty->get();
  ret->ord_status = trading::ord_status_from_char(ord_status->get());
  ret->side = common::charToSide(side->get() - 1);  // 1: Buy, 2: Sell

  if (likely(leaves_qty != nullptr))
    ret->leaves_qty.value = leaves_qty->get();

  if (likely(price != nullptr))
    ret->price.value = price->get();

  if (error_code != nullptr)
    ret->error_code = error_code->get();

  if (text != nullptr)
    ret->text = text->get();

  return ret;
}

trading::OrderCancelReject* FixOeCore::create_order_cancel_reject_message(
    FIX8::NewOroFix44OE::OrderCancelReject* msg) {
  const auto* cl_order_id = msg->get<ClOrdID>();
  const auto* symbol = msg->get<Symbol>();
  const auto* error_code = msg->get<ErrorCode>();
  const auto* text = msg->get<Text>();

  auto* ret = response_manager_->order_cancel_reject_allocate();

  ret->cl_order_id = common::OrderId{std::stoull(cl_order_id->get())};
  ret->symbol = symbol->get();

  if (error_code != nullptr)
    ret->error_code = error_code->get();

  if (text != nullptr)
    ret->text = text->get();

  return ret;
}

trading::OrderMassCancelReport*
FixOeCore::create_order_mass_cancel_report_message(
    FIX8::NewOroFix44OE::OrderMassCancelReport* msg) {
  const auto* cl_order_id = msg->get<ClOrdID>();
  const auto* symbol = msg->get<Symbol>();
  const auto* error_code = msg->get<ErrorCode>();
  const auto* response = msg->get<MassCancelResponse>();
  const auto* mass_cancel_request_type = msg->get<MassCancelRequestType>();
  const auto* total_affected_orders = msg->get<TotalAffectedOrders>();
  const auto* text = msg->get<Text>();

  auto* ret = response_manager_->order_mass_cancel_report_allocate();

  ret->cl_order_id = common::OrderId{std::stoull(cl_order_id->get())};
  ret->symbol = symbol->get();

  if (error_code != nullptr)
    ret->error_code = error_code->get();

  ret->mass_cancel_response =
      trading::mass_cancel_response_from_char(response->get());
  ret->mass_cancel_request_type = mass_cancel_request_type->get();

  if (total_affected_orders != nullptr)
    ret->total_affected_orders = total_affected_orders->get();

  if (text != nullptr)
    ret->text = text->get();

  return ret;
}

trading::OrderReject FixOeCore::create_reject_message(
    FIX8::NewOroFix44OE::Reject* msg) {
  const auto msg_type = msg->get<RefMsgType>();
  const auto rejected_type = msg->get<SessionRejectReason>();
  const auto error_message = msg->get<Text>();
  const auto error_code = msg->get<ErrorCode>();

  return trading::OrderReject{.session_reject_reason = msg_type->get(),
                              .rejected_message_type = rejected_type->get(),
                              .error_message = error_message->get(),
                              .error_code = error_code->get()};
}

FIX8::Message* FixOeCore::decode(const std::string& message) {
  START_MEASURE(OE_Convert_Message);
  FIX8::Message* msg(FIX8::Message::factory(ctx(), message, true, true));
  END_MEASURE(OE_Convert_Message, logger_);
  if (likely(msg)) {
    return msg;
  }
  return nullptr;
}

}  // namespace core
