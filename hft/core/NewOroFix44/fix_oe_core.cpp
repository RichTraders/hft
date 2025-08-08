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

#include "fix_oe_core.h"
#include <fix8/f8includes.hpp>
#include "NewOroFix44OE_types.hpp"
#include "NewOroFix44OE_router.hpp"
#include "NewOroFix44OE_classes.hpp"
#include "performance.h"
#include "signature.h"

namespace core {
using namespace FIX8::NewOroFix44OE;

FixOeCore::FixOeCore(SendId sender_comp_id, TargetId target_comp_id,
                     common::Logger* logger, const Authorization& authorization)
    : sender_comp_id_(std::move(sender_comp_id)),
      target_comp_id_(std::move(target_comp_id)),
      logger_(logger),
      authorization_(authorization) {}

std::string FixOeCore::create_log_on_message(const std::string& sig_b64,
                                             const std::string& timestamp) {
  FIX8::NewOroFix44OE_ctx();  // 왜하는겨?
  Logon request;

  FIX8::MessageBase* header = request.Header();
  *header << new SenderCompID(sender_comp_id_)
          << new TargetCompID(target_comp_id_) << new MsgSeqNum(sequence_++)
          << new SendingTime(timestamp);

  request << new EncryptMethod(EncryptMethod_NONE) << new HeartBtInt(30)
          << new ResetSeqNumFlag(true) << new MessageHandling('0')
          << new ResponseMode(1) << new DropCopyFlag(false)
          << new RawDataLength(static_cast<int>(sig_b64.size()))
          << new RawData(sig_b64) << new Username(authorization_.api_key)
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
          << new TargetCompID(target_comp_id_)
          << new MsgSeqNum(sequence_++)
          << new SendingTime();

  request.add_field(new ClOrdID(std::to_string(order_data.cl_order_id.value)));
  request.add_field(new Symbol(order_data.symbol));
  request.add_field(new Side(trading::to_char(order_data.side)));
  request.add_field(new OrdType(trading::to_char(order_data.ord_type)));
  request.add_field(new OrderQty(order_data.order_qty));
  request.add_field(new SelfTradePreventionMode(
      trading::to_char(order_data.self_trade_prevention_mode)));

  if (order_data.ord_type == trading::OrderType::kLimit) {
    // Limit 주문일 때만
    request.add_field(new Price(order_data.price));
    request.add_field(
        new TimeInForce(trading::to_char(order_data.time_in_force)));
  }

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

  request.add_field(new ClOrdID(std::to_string(cancel_request.cl_order_id.value)));
  request.add_field(new Symbol(cancel_request.symbol));

  std::string wire;
  request.encode(wire);
  return wire;
}

std::string FixOeCore::create_cancel_and_reorder_message(
    const trading::OrderCancelRequestAndNewOrderSingle& cancel_and_re_order) {
  OrderCancelRequestAndNewOrderSingle request;

  FIX8::MessageBase* header = request.Header();
  *header << new SenderCompID(sender_comp_id_)
          << new TargetCompID(target_comp_id_) << new MsgSeqNum(sequence_++)
          << new SendingTime();

  request.add_field(new OrderID(cancel_and_re_order.cancel_ord_id));
  request.add_field(new ClOrdID(std::to_string(cancel_and_re_order.cl_order_id.value)));
  request.add_field(new Symbol(cancel_and_re_order.symbol));
  request.add_field(new Side(trading::to_char(cancel_and_re_order.side)));
  request.add_field(
      new OrdType(trading::to_char(cancel_and_re_order.ord_type)));
  request.add_field(new OrderQty(cancel_and_re_order.order_qty));
  request.add_field(new SelfTradePreventionMode(
      trading::to_char(cancel_and_re_order.self_trade_prevention_mode)));
  request.add_field(
      new OrderCancelRequestAndNewOrderSingleMode(trading::to_char(
          cancel_and_re_order.order_cancel_request_and_new_order_single_mode)));

  if (cancel_and_re_order.ord_type == trading::OrderType::kLimit) {
    // Limit 주문일 때만
    request.add_field(new Price(cancel_and_re_order.price));
    request.add_field(
        new TimeInForce(trading::to_char(cancel_and_re_order.time_in_force)));
  }

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

  request.add_field(new ClOrdID(std::to_string(all_order_cancel.cl_order_id.value)));
  request.add_field(new Symbol(all_order_cancel.symbol));
  request.add_field(
      new MassCancelRequestType(all_order_cancel.mass_cancel_request_type));

  std::string wire;
  request.encode(wire);
  return wire;
}

trading::ExecutionReport* FixOeCore::create_excution_report_message(
    ExecutionReport* msg) {
  const auto cl_order_id = msg->get<ClOrdID>();
  const auto order_id = msg->get<OrderID>();
  const auto symbol = msg->get<Symbol>();
  const auto exec_type = msg->get<ExecType>();
  const auto ord_status = msg->get<OrdStatus>();
  const auto cum_qty = msg->get<CumQty>();
  const auto leaves_qty = msg->get<LeavesQty>();
  const auto last_qty = msg->get<LastQty>();
  const auto price = msg->get<Price>();
  const auto reason = msg->get<OrdRejReason>();

  auto* ret = new trading::ExecutionReport;

  ret->symbol = symbol->get();
  ret->order_id = common::OrderId{std::stoull(cl_order_id->get())};
  ret->cum_qty.value = cum_qty->get();
  ret->exec_type = trading::exec_type_from_char(exec_type->get());
  ret->last_qty.value = last_qty->get();
  ret->ord_status = trading::ord_status_from_char(ord_status->get());

  if (likely(leaves_qty != nullptr))
    ret->leaves_qty.value = leaves_qty->get();

  if (likely(order_id != nullptr))
    ret->order_id.value = order_id->get();

  if (likely(price != nullptr))
    ret->price.value = price->get();

  if (likely(reason != nullptr))
    ret->reason = reason->get();

  return ret;
}

trading::OrderCancelReject* FixOeCore::create_order_cancel_reject_message(
    FIX8::NewOroFix44OE::OrderCancelReject* msg) {
  auto cl_order_id = msg->get<ClOrdID>();
  auto orderId = msg->get<OrderID>();
  auto symbol = msg->get<Symbol>();
  auto error_code = msg->get<ErrorCode>();

  trading::OrderCancelReject* ret = new trading::OrderCancelReject;

  ret->cl_order_id = cl_order_id->get();
  ret->order_id.value = orderId->get();
  ret->symbol = symbol->get();

  if (error_code != nullptr)
    ret->error_code = error_code->get();

  return ret;
}

trading::OrderMassCancelReport*
FixOeCore::create_order_mass_cancel_report_message(
    FIX8::NewOroFix44OE::OrderMassCancelReport* msg) {
  auto cl_order_id = msg->get<ClOrdID>();
  auto symbol = msg->get<Symbol>();
  auto error_code = msg->get<ErrorCode>();
  auto response = msg->get<MassCancelResponse>();
  auto mass_cancel_request_type = msg->get<MassCancelRequestType>();
  auto total_affected_orders = msg->get<TotalAffectedOrders>();

  trading::OrderMassCancelReport* ret = new trading::OrderMassCancelReport;

  ret->cl_order_id = cl_order_id->get();
  ret->symbol = symbol->get();

  if (error_code != nullptr)
    ret->error_code = error_code->get();

  ret->mass_cancel_response =
      trading::mass_cancel_response_from_char(response->get());
  ret->mass_cancel_request_type = mass_cancel_request_type->get();

  if (total_affected_orders != nullptr)
    ret->total_affected_orders = total_affected_orders->get();

  return ret;
}

FIX8::Message* FixOeCore::decode(const std::string& message) {
#ifdef DEBUG
  START_MEASURE(Convert_Message);
#endif
  FIX8::Message* msg(FIX8::Message::factory(ctx(), message, true, true));
#ifdef DEBUG
  END_MEASURE(Convert_Message, logger_);
#endif
  if (likely(msg)) {
    return msg;
  }
  return nullptr;
}

}  // namespace core