//
// Created by neworo2 on 25. 7. 26.
//

#include "fix_oe_core.h"
#include <fix8/f8includes.hpp>
#include "NewOroFix44OE_types.hpp"
#include "NewOroFix44OE_router.hpp"
#include "NewOroFix44OE_classes.hpp"
#include "signature.h"

namespace core {
using namespace FIX8::NewOroFix44OE;

FixOeCore::FixOeCore(SendId sender_comp_id,
                     TargetId target_comp_id,
                     common::Logger* logger,
                     common::MemoryPool<OrderData>* pool)
  :
  sender_comp_id_(std::move(sender_comp_id)),
  target_comp_id_(std::move(target_comp_id)),
  logger_(logger),
  order_data_pool_(pool) {}

std::string FixOeCore::create_log_on_message(const std::string& sig_b64,
                                             const std::string& timestamp) {
  FIX8::NewOroFix44OE_ctx(); // 왜하는겨?
  Logon request;

  FIX8::MessageBase* header = request.Header();
  *header
      << new SenderCompID(sender_comp_id_)
      << new TargetCompID(target_comp_id_)
      << new MsgSeqNum(sequence_++)
      << new SendingTime(timestamp);

  request << new EncryptMethod(EncryptMethod_NONE)
      << new HeartBtInt(30)
      << new ResetSeqNumFlag(true)
      << new MessageHandling('0')
      << new ResponseMode(1)
      << new DropCopyFlag(false)
      << new RawDataLength(static_cast<int>(sig_b64.size()))
      << new RawData(sig_b64)
      << new Username(
#ifndef UNIT_TEST
          "psxtGrh4X1aLsBVoMn3NCEuHDns78Yie9BMO0TIJEJvLFpZKk86guB7aOqsYTVk2")
#else
          "cJHjHNqHUG1nhTs0YPEKlmxoXokNomptrrilcGzrhoqhd8S9kEFfcJg2YQjVKgGw")
#endif
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

std::string FixOeCore::create_order_message(const trading::NewSingleOrderData& order_data) {
  NewOrderSingle request;

  FIX8::MessageBase* header = request.Header();
  *header
      << new SenderCompID(sender_comp_id_)
      << new TargetCompID(target_comp_id_)
      << new MsgSeqNum(sequence_++)
      << new SendingTime(order_data.transactTime);

  request.add_field(new ClOrdID(order_data.cl_order_id));
  request.add_field(new Symbol(order_data.symbol));
  request.add_field(new Side(trading::toChar(order_data.side)));
  request.add_field(new OrdType(trading::toChar(order_data.ord_type)));
  request.add_field(new OrderQty(order_data.order_qty));

  if (order_data.ord_type == trading::OrderType::kLimit) {
    // Limit 주문일 때만
    request.add_field(new Price(order_data.price));
    request.add_field(new TimeInForce(trading::toChar(order_data.time_in_force)));
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

trading::ExecutionReport FixOeCore::create_excution_report_message(
    FIX8::NewOroFix44OE::ExecutionReport* msg) {
  auto clOrdId = msg->get<ClOrdID>();
  auto orderId = msg->get<OrderID>();
  auto symbol = msg->get<Symbol>();
  auto execType = msg->get<ExecType>();
  auto ordStatus = msg->get<OrdStatus>();
  auto cumQty = msg->get<CumQty>();
  auto leavesQty = msg->get<LeavesQty>();
  auto lastQty = msg->get<LastQty>();
  auto price = msg->get<Price>();
  auto reason = msg->get<OrdRejReason>();
  auto txt = msg->get<Text>();

  trading::ExecutionReport ret;

  ret.symbol = symbol->get();
  ret.cl_ord_id = clOrdId->get();
  ret.cum_qty = cumQty->get();
  ret.exec_type = trading::execTypeFromChar(execType->get());
  ret.last_qty = lastQty->get();
  ret.leaves_qty = leavesQty->get();
  ret.ord_status = trading::ordStatusFromChar(ordStatus->get());

  if (orderId != nullptr)
    ret.order_id = orderId->get();

  if (price != nullptr)
    ret.price = price->get();

  if (reason != nullptr)
    ret.reason = reason->get();

  if (txt != nullptr)
    ret.text = txt->get();

  return ret;
}

FIX8::Message* FixOeCore::decode(const std::string& message) {
#ifdef DEBUG
  START_MEASURE(Convert_Message);
#endif
  FIX8::Message* msg(
      FIX8::Message::factory(ctx(), message, true, true));
#ifdef DEBUG
  END_MEASURE(Convert_Message);
#endif
  if (likely(msg)) {
    return msg;
  }
  return nullptr;
}

}