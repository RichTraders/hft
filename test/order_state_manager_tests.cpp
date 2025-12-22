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

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "order_state_manager.h"
#include "layer_book.h"
#include "logger.h"
#include "reserved_position_tracker.h"
#include "orders.h"

using namespace trading;
using namespace common;
using namespace order;

class OrderStateManagerTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    logger_ = std::make_unique<Logger>();
    logger_->setLevel(LogLevel::kDebug);
    logger_->clearSink();
    logger_->addSink(std::make_unique<ConsoleSink>());
  }

  static void TearDownTestSuite() {
    logger_->shutdown();
    logger_.reset();
  }

  void SetUp() override {
    producer_ = std::make_unique<Logger::Producer>(logger_->make_producer());
    tick_converter_ = std::make_unique<TickConverter>(0.01);  // 0.01 tick size
    state_manager_ = std::make_unique<OrderStateManager>(*producer_, *tick_converter_);
    position_tracker_ = std::make_unique<ReservedPositionTracker>();

    // Setup side book with default ticker
    side_book_ = std::make_unique<SideBook>();
  }

  void TearDown() override {
    side_book_.reset();
    position_tracker_.reset();
    state_manager_.reset();
    tick_converter_.reset();
    producer_.reset();
  }

  static std::unique_ptr<Logger> logger_;
  std::unique_ptr<Logger::Producer> producer_;
  std::unique_ptr<TickConverter> tick_converter_;
  std::unique_ptr<OrderStateManager> state_manager_;
  std::unique_ptr<ReservedPositionTracker> position_tracker_;
  std::unique_ptr<SideBook> side_book_;
};

std::unique_ptr<Logger> OrderStateManagerTest::logger_;

// ============================================================================
// PendingNew Tests
// ============================================================================

TEST_F(OrderStateManagerTest, HandlePendingNew_TransitionsToCorrectState) {
  // Setup: Reserve a slot
  const int layer = 0;
  const OrderId order_id{12345};
  const Price price{100.50};

  side_book_->slots[layer].state = OMOrderState::kReserved;
  side_book_->slots[layer].cl_order_id = order_id;
  side_book_->slots[layer].price = price;
  side_book_->slots[layer].qty = Qty{1.0};
  side_book_->layer_ticks[layer] = tick_converter_->to_ticks(price.value);

  // Create execution report
  ExecutionReport report;
  report.ord_status = OrdStatus::kPendingNew;
  report.cl_order_id = order_id;
  report.price = price;
  report.side = Side::kBuy;

  // Execute
  state_manager_->handle_execution_report(&report, *side_book_, *position_tracker_, 0);

  // Verify
  EXPECT_EQ(side_book_->slots[layer].state, OMOrderState::kPendingNew);
}

// ============================================================================
// New Order Tests
// ============================================================================

TEST_F(OrderStateManagerTest, HandleNew_SimpleNewOrder_TransitionsToLive) {
  // Setup
  const int layer = 0;
  const OrderId order_id{12345};
  const Price price{100.50};
  const Qty qty{1.5};

  side_book_->slots[layer].state = OMOrderState::kReserved;
  side_book_->slots[layer].cl_order_id = order_id;
  side_book_->slots[layer].price = price;
  side_book_->slots[layer].qty = qty;
  side_book_->layer_ticks[layer] = tick_converter_->to_ticks(price.value);

  // Create execution report
  ExecutionReport report;
  report.ord_status = OrdStatus::kNew;
  report.cl_order_id = order_id;
  report.price = price;
  report.leaves_qty = qty;
  report.side = Side::kBuy;

  // Execute
  state_manager_->handle_execution_report(&report, *side_book_, *position_tracker_, 0);

  // Verify
  EXPECT_EQ(side_book_->slots[layer].state, OMOrderState::kLive);
  EXPECT_EQ(side_book_->slots[layer].qty, qty);
}

TEST_F(OrderStateManagerTest, HandleNew_CancelAndReorder_ProcessesPendingReplace) {
  // Setup: Order with pending replace
  const int layer = 0;
  const OrderId old_id{12345};
  const OrderId new_id{12346};
  const Price old_price{100.50};
  const Price new_price{101.00};
  const Qty old_qty{1.0};
  const Qty new_qty{2.0};

  side_book_->slots[layer].state = OMOrderState::kCancelReserved;
  side_book_->slots[layer].cl_order_id = old_id;
  side_book_->slots[layer].price = old_price;
  side_book_->slots[layer].qty = old_qty;

  // Set pending replace info
  side_book_->pending_repl[layer] = PendingReplaceInfo(
    new_price,
    new_qty,
    tick_converter_->to_ticks(new_price.value),
    new_id,
    old_qty,
    old_id,
    old_price,
    tick_converter_->to_ticks(old_price.value)
  );

  side_book_->new_id_to_layer[new_id.value] = layer;

  // Create execution report for new order
  ExecutionReport report;
  report.ord_status = OrdStatus::kNew;
  report.cl_order_id = new_id;
  report.price = new_price;
  report.leaves_qty = new_qty;
  report.side = Side::kBuy;

  // Execute
  state_manager_->handle_execution_report(&report, *side_book_, *position_tracker_, 0);

  // Verify
  EXPECT_EQ(side_book_->slots[layer].state, OMOrderState::kLive);
  EXPECT_EQ(side_book_->slots[layer].price, new_price);
  EXPECT_EQ(side_book_->slots[layer].qty, new_qty);
  EXPECT_EQ(side_book_->slots[layer].cl_order_id, new_id);
  EXPECT_FALSE(side_book_->pending_repl[layer].has_value());
}

// ============================================================================
// PartiallyFilled Tests
// ============================================================================

TEST_F(OrderStateManagerTest, HandlePartiallyFilled_UpdatesQuantityAndPosition) {
  // Setup
  const int layer = 0;
  const OrderId order_id{12345};
  const Price price{100.50};
  const Qty initial_qty{10.0};
  const Qty remaining_qty{6.0};

  side_book_->slots[layer].state = OMOrderState::kLive;
  side_book_->slots[layer].cl_order_id = order_id;
  side_book_->slots[layer].price = price;
  side_book_->slots[layer].qty = initial_qty;
  side_book_->layer_ticks[layer] = tick_converter_->to_ticks(price.value);

  position_tracker_->add_reserved(Side::kBuy, initial_qty);

  // Create execution report
  ExecutionReport report;
  report.ord_status = OrdStatus::kPartiallyFilled;
  report.cl_order_id = order_id;
  report.price = price;
  report.leaves_qty = remaining_qty;
  report.side = Side::kBuy;

  const auto initial_reserved = position_tracker_->get_reserved();

  // Execute
  state_manager_->handle_execution_report(&report, *side_book_, *position_tracker_, 1000);

  // Verify
  EXPECT_EQ(side_book_->slots[layer].state, OMOrderState::kLive);
  EXPECT_EQ(side_book_->slots[layer].qty, remaining_qty);

  // Reserved position should decrease by filled amount (4.0)
  const Qty filled_qty = initial_qty - remaining_qty;
  EXPECT_DOUBLE_EQ(position_tracker_->get_reserved().value,
                   initial_reserved.value - filled_qty.value);
}

TEST_F(OrderStateManagerTest, HandlePartiallyFilled_FullyFilled_TransitionsToDead) {
  // Setup
  const int layer = 0;
  const OrderId order_id{12345};
  const Price price{100.50};
  const Qty initial_qty{10.0};
  const Qty remaining_qty{0.0};

  side_book_->slots[layer].state = OMOrderState::kLive;
  side_book_->slots[layer].cl_order_id = order_id;
  side_book_->slots[layer].price = price;
  side_book_->slots[layer].qty = initial_qty;
  side_book_->layer_ticks[layer] = tick_converter_->to_ticks(price.value);

  // Create execution report
  ExecutionReport report;
  report.ord_status = OrdStatus::kPartiallyFilled;
  report.cl_order_id = order_id;
  report.price = price;
  report.leaves_qty = remaining_qty;
  report.side = Side::kBuy;

  // Execute
  state_manager_->handle_execution_report(&report, *side_book_, *position_tracker_, 1000);

  // Verify
  EXPECT_EQ(side_book_->slots[layer].state, OMOrderState::kDead);
}

// ============================================================================
// Filled Tests
// ============================================================================

TEST_F(OrderStateManagerTest, HandleFilled_TransitionsToDeadAndClearsReserved) {
  // Setup
  const int layer = 0;
  const OrderId order_id{12345};
  const Price price{100.50};
  const Qty qty{5.0};

  side_book_->slots[layer].state = OMOrderState::kLive;
  side_book_->slots[layer].cl_order_id = order_id;
  side_book_->slots[layer].price = price;
  side_book_->slots[layer].qty = qty;
  side_book_->layer_ticks[layer] = tick_converter_->to_ticks(price.value);

  position_tracker_->add_reserved(Side::kBuy, qty);

  // Create execution report
  ExecutionReport report;
  report.ord_status = OrdStatus::kFilled;
  report.cl_order_id = order_id;
  report.price = price;
  report.leaves_qty = Qty{0.0};
  report.side = Side::kBuy;

  // Execute
  state_manager_->handle_execution_report(&report, *side_book_, *position_tracker_, 1000);

  // Verify
  EXPECT_EQ(side_book_->slots[layer].state, OMOrderState::kDead);
  EXPECT_DOUBLE_EQ(position_tracker_->get_reserved().value, 0.0);
}

// ============================================================================
// PendingCancel Tests
// ============================================================================

TEST_F(OrderStateManagerTest, HandlePendingCancel_TransitionsToCorrectState) {
  // Setup
  const int layer = 0;
  const OrderId order_id{12345};
  const Price price{100.50};

  side_book_->slots[layer].state = OMOrderState::kLive;
  side_book_->slots[layer].cl_order_id = order_id;
  side_book_->slots[layer].price = price;
  side_book_->slots[layer].qty = Qty{1.0};
  side_book_->layer_ticks[layer] = tick_converter_->to_ticks(price.value);

  // Create execution report
  ExecutionReport report;
  report.ord_status = OrdStatus::kPendingCancel;
  report.cl_order_id = order_id;
  report.price = price;
  report.side = Side::kBuy;

  // Execute
  state_manager_->handle_execution_report(&report, *side_book_, *position_tracker_, 0);

  // Verify
  EXPECT_EQ(side_book_->slots[layer].state, OMOrderState::kPendingCancel);
}

// ============================================================================
// Canceled Tests
// ============================================================================

TEST_F(OrderStateManagerTest, HandleCanceled_SimpleCancelTransitionsToDead) {
  // Setup
  const int layer = 0;
  const OrderId order_id{12345};
  const Price price{100.50};
  const Qty qty{3.0};

  side_book_->slots[layer].state = OMOrderState::kPendingCancel;
  side_book_->slots[layer].cl_order_id = order_id;
  side_book_->slots[layer].price = price;
  side_book_->slots[layer].qty = qty;
  side_book_->layer_ticks[layer] = tick_converter_->to_ticks(price.value);

  position_tracker_->add_reserved(Side::kBuy, qty);

  // Create execution report
  ExecutionReport report;
  report.ord_status = OrdStatus::kCanceled;
  report.cl_order_id = order_id;
  report.price = price;
  report.side = Side::kBuy;

  // Execute
  state_manager_->handle_execution_report(&report, *side_book_, *position_tracker_, 0);

  // Verify
  EXPECT_EQ(side_book_->slots[layer].state, OMOrderState::kDead);
  EXPECT_DOUBLE_EQ(position_tracker_->get_reserved().value, 0.0);
}

TEST_F(OrderStateManagerTest, HandleCanceled_CancelForReplace_TransitionsToReserved) {
  // Setup: Cancel as part of replace operation
  const int layer = 0;
  const OrderId old_id{12345};

  side_book_->slots[layer].state = OMOrderState::kCancelReserved;
  side_book_->slots[layer].cl_order_id = old_id;
  side_book_->slots[layer].price = Price{100.0};
  side_book_->slots[layer].qty = Qty{1.0};

  // Map this order as being replaced
  side_book_->orig_id_to_layer[old_id.value] = layer;

  // Create execution report
  ExecutionReport report;
  report.ord_status = OrdStatus::kCanceled;
  report.cl_order_id = old_id;
  report.price = Price{100.0};
  report.side = Side::kBuy;

  // Execute
  state_manager_->handle_execution_report(&report, *side_book_, *position_tracker_, 0);

  // Verify
  EXPECT_EQ(side_book_->slots[layer].state, OMOrderState::kReserved);
  EXPECT_EQ(side_book_->orig_id_to_layer.count(old_id.value), 0);
}

// ============================================================================
// Rejected/Expired Tests
// ============================================================================

TEST_F(OrderStateManagerTest, HandleRejected_SimpleReject_TransitionsToDead) {
  // Setup
  const int layer = 0;
  const OrderId order_id{12345};
  const Price price{100.50};
  const Qty qty{2.0};

  side_book_->slots[layer].state = OMOrderState::kReserved;
  side_book_->slots[layer].cl_order_id = order_id;
  side_book_->slots[layer].price = price;
  side_book_->slots[layer].qty = qty;
  side_book_->layer_ticks[layer] = tick_converter_->to_ticks(price.value);

  position_tracker_->add_reserved(Side::kBuy, qty);

  // Create execution report
  ExecutionReport report;
  report.ord_status = OrdStatus::kRejected;
  report.cl_order_id = order_id;
  report.price = price;
  report.side = Side::kBuy;

  // Execute
  state_manager_->handle_execution_report(&report, *side_book_, *position_tracker_, 0);

  // Verify
  EXPECT_EQ(side_book_->slots[layer].state, OMOrderState::kDead);
  EXPECT_DOUBLE_EQ(position_tracker_->get_reserved().value, 0.0);
}

TEST_F(OrderStateManagerTest, HandleRejected_ReplaceRejected_RestoresOriginalState) {
  // Setup: Rejected replace operation
  const int layer = 0;
  const OrderId old_id{12345};
  const OrderId new_id{12346};
  const Price old_price{100.0};
  const Price new_price{101.0};
  const Qty old_qty{5.0};
  const Qty new_qty{7.0};

  side_book_->slots[layer].state = OMOrderState::kCancelReserved;
  side_book_->slots[layer].cl_order_id = new_id;
  side_book_->slots[layer].price = new_price;
  side_book_->slots[layer].qty = new_qty;

  // Set pending replace info
  side_book_->pending_repl[layer] = PendingReplaceInfo(
    new_price,
    new_qty,
    tick_converter_->to_ticks(new_price.value),
    new_id,
    old_qty,
    old_id,
    old_price,
    tick_converter_->to_ticks(old_price.value)
  );

  side_book_->new_id_to_layer[new_id.value] = layer;

  const Qty delta_qty = new_qty - old_qty;
  position_tracker_->add_reserved(Side::kBuy, old_qty);
  position_tracker_->add_reserved(Side::kBuy, delta_qty);

  // Create execution report
  ExecutionReport report;
  report.ord_status = OrdStatus::kRejected;
  report.cl_order_id = new_id;
  report.price = new_price;
  report.side = Side::kBuy;

  // Execute
  state_manager_->handle_execution_report(&report, *side_book_, *position_tracker_, 0);

  // Verify: Order should be restored to original state
  EXPECT_EQ(side_book_->slots[layer].state, OMOrderState::kLive);
  EXPECT_EQ(side_book_->slots[layer].cl_order_id, old_id);
  EXPECT_EQ(side_book_->slots[layer].price, old_price);
  EXPECT_EQ(side_book_->slots[layer].qty, old_qty);
  EXPECT_FALSE(side_book_->pending_repl[layer].has_value());
  EXPECT_EQ(side_book_->new_id_to_layer.count(new_id.value), 0);
  EXPECT_DOUBLE_EQ(position_tracker_->get_reserved().value, old_qty.value);
}

TEST_F(OrderStateManagerTest, HandleExpired_TransitionsToDead) {
  // Setup
  const int layer = 0;
  const OrderId order_id{12345};
  const Price price{100.50};
  const Qty qty{2.5};

  side_book_->slots[layer].state = OMOrderState::kLive;
  side_book_->slots[layer].cl_order_id = order_id;
  side_book_->slots[layer].price = price;
  side_book_->slots[layer].qty = qty;
  side_book_->layer_ticks[layer] = tick_converter_->to_ticks(price.value);

  position_tracker_->add_reserved(Side::kBuy, qty);

  // Create execution report
  ExecutionReport report;
  report.ord_status = OrdStatus::kExpired;
  report.cl_order_id = order_id;
  report.price = price;
  report.side = Side::kBuy;

  // Execute
  state_manager_->handle_execution_report(&report, *side_book_, *position_tracker_, 0);

  // Verify
  EXPECT_EQ(side_book_->slots[layer].state, OMOrderState::kDead);
  EXPECT_DOUBLE_EQ(position_tracker_->get_reserved().value, 0.0);
}
