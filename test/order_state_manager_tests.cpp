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
    // tick_size must match kPriceScale: kPriceScale=10 -> tick_size=0.1
    tick_converter_ = std::make_unique<TickConverter>(0.1);
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
  const auto price = PriceType::from_double(100.50);

  side_book_->slots[layer].state = OMOrderState::kReserved;
  side_book_->slots[layer].cl_order_id = order_id;
  side_book_->slots[layer].price = price;
  side_book_->slots[layer].qty = QtyType::from_double(1.0);
  side_book_->layer_ticks[layer] = tick_converter_->to_ticks(price.to_double());

  // Create execution report
  ExecutionReport report;
  report.ord_status = OrdStatus::kPendingNew;
  report.cl_order_id = order_id;
  report.price = PriceType{price};
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
  const auto price = PriceType::from_double(100.50);
  const auto qty = QtyType::from_double(1.5);

  side_book_->slots[layer].state = OMOrderState::kReserved;
  side_book_->slots[layer].cl_order_id = order_id;
  side_book_->slots[layer].price = price;
  side_book_->slots[layer].qty = qty;
  side_book_->layer_ticks[layer] = tick_converter_->to_ticks(price.to_double());

  // Create execution report
  ExecutionReport report;
  report.ord_status = OrdStatus::kNew;
  report.cl_order_id = order_id;
  report.price = PriceType{price};
  report.leaves_qty = qty;
  report.side = Side::kBuy;

  // Execute
  state_manager_->handle_execution_report(&report, *side_book_, *position_tracker_, 0);

  // Verify
  EXPECT_EQ(side_book_->slots[layer].state, OMOrderState::kLive);
  EXPECT_EQ(side_book_->slots[layer].qty.value, qty.value);
}

TEST_F(OrderStateManagerTest, HandleNew_CancelAndReorder_ProcessesPendingReplace) {
  // Setup: Order with pending replace
  const int layer = 0;
  const OrderId old_id{12345};
  const OrderId new_id{12346};
  const auto old_price = PriceType::from_double(100.50);
  const auto new_price = PriceType::from_double(101.00);
  const auto old_qty = QtyType::from_double(1.0);
  const auto new_qty = QtyType::from_double(2.0);

  side_book_->slots[layer].state = OMOrderState::kCancelReserved;
  side_book_->slots[layer].cl_order_id = old_id;
  side_book_->slots[layer].price = old_price;
  side_book_->slots[layer].qty = old_qty;

  // Set pending replace info
  side_book_->pending_repl[layer] = PendingReplaceInfo(
    new_price,
    new_qty,
    tick_converter_->to_ticks(new_price.to_double()),
    new_id,
    old_qty,
    old_id,
    old_price,
    tick_converter_->to_ticks(old_price.to_double())
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
  EXPECT_EQ(side_book_->slots[layer].price.value, new_price.value);
  EXPECT_EQ(side_book_->slots[layer].qty.value, new_qty.value);
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
  const auto price = PriceType::from_double(100.50);
  const auto initial_qty = QtyType::from_double(10.0);
  const auto remaining_qty = QtyType::from_double(6.0);

  side_book_->slots[layer].state = OMOrderState::kLive;
  side_book_->slots[layer].cl_order_id = order_id;
  side_book_->slots[layer].price = price;
  side_book_->slots[layer].qty = initial_qty;
  side_book_->layer_ticks[layer] = tick_converter_->to_ticks(price.to_double());

  position_tracker_->add_reserved(Side::kBuy, initial_qty.value);

  // Create execution report
  ExecutionReport report;
  report.ord_status = OrdStatus::kPartiallyFilled;
  report.cl_order_id = order_id;
  report.price = PriceType{price};
  report.leaves_qty = remaining_qty;
  report.side = Side::kBuy;

  const auto initial_reserved = position_tracker_->get_reserved();

  // Execute
  state_manager_->handle_execution_report(&report, *side_book_, *position_tracker_, 1000);

  // Verify
  EXPECT_EQ(side_book_->slots[layer].state, OMOrderState::kLive);
  EXPECT_EQ(side_book_->slots[layer].qty.value, remaining_qty.value);

  // Reserved position should decrease by filled amount (4.0)
  const int64_t filled_qty = initial_qty.value - remaining_qty.value;
  EXPECT_EQ(position_tracker_->get_reserved(),
                   initial_reserved - filled_qty);
}

TEST_F(OrderStateManagerTest, HandlePartiallyFilled_FullyFilled_TransitionsToDead) {
  // Setup
  const int layer = 0;
  const OrderId order_id{12345};
  const auto price = PriceType::from_double(100.50);
  const auto initial_qty = QtyType::from_double(10.0);
  const auto remaining_qty = QtyType::from_double(0.0);

  side_book_->slots[layer].state = OMOrderState::kLive;
  side_book_->slots[layer].cl_order_id = order_id;
  side_book_->slots[layer].price = price;
  side_book_->slots[layer].qty = initial_qty;
  side_book_->layer_ticks[layer] = tick_converter_->to_ticks(price.to_double());

  // Create execution report
  ExecutionReport report;
  report.ord_status = OrdStatus::kPartiallyFilled;
  report.cl_order_id = order_id;
  report.price = PriceType{price};
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
  const auto price = PriceType::from_double(100.50);
  const auto qty = QtyType::from_double(5.0);

  side_book_->slots[layer].state = OMOrderState::kLive;
  side_book_->slots[layer].cl_order_id = order_id;
  side_book_->slots[layer].price = price;
  side_book_->slots[layer].qty = qty;
  side_book_->layer_ticks[layer] = tick_converter_->to_ticks(price.to_double());

  position_tracker_->add_reserved(Side::kBuy, qty.value);

  // Create execution report
  ExecutionReport report;
  report.ord_status = OrdStatus::kFilled;
  report.cl_order_id = order_id;
  report.price = PriceType{price};
  report.leaves_qty = QtyType::from_double(0.0);
  report.side = Side::kBuy;

  // Execute
  state_manager_->handle_execution_report(&report, *side_book_, *position_tracker_, 1000);

  // Verify
  EXPECT_EQ(side_book_->slots[layer].state, OMOrderState::kDead);
  EXPECT_EQ(position_tracker_->get_reserved(), 0);
}

// ============================================================================
// PendingCancel Tests
// ============================================================================

TEST_F(OrderStateManagerTest, HandlePendingCancel_TransitionsToCorrectState) {
  // Setup
  const int layer = 0;
  const OrderId order_id{12345};
  const auto price = PriceType::from_double(100.50);

  side_book_->slots[layer].state = OMOrderState::kLive;
  side_book_->slots[layer].cl_order_id = order_id;
  side_book_->slots[layer].price = price;
  side_book_->slots[layer].qty = QtyType::from_double(1.0);
  side_book_->layer_ticks[layer] = tick_converter_->to_ticks(price.to_double());

  // Create execution report
  ExecutionReport report;
  report.ord_status = OrdStatus::kPendingCancel;
  report.cl_order_id = order_id;
  report.price = PriceType{price};
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
  const auto price = PriceType::from_double(100.50);
  const auto qty = QtyType::from_double(3.0);

  side_book_->slots[layer].state = OMOrderState::kPendingCancel;
  side_book_->slots[layer].cl_order_id = order_id;
  side_book_->slots[layer].price = price;
  side_book_->slots[layer].qty = qty;
  side_book_->layer_ticks[layer] = tick_converter_->to_ticks(price.to_double());

  position_tracker_->add_reserved(Side::kBuy, qty.value);

  // Create execution report
  ExecutionReport report;
  report.ord_status = OrdStatus::kCanceled;
  report.cl_order_id = order_id;
  report.price = PriceType{price};
  report.side = Side::kBuy;

  // Execute
  state_manager_->handle_execution_report(&report, *side_book_, *position_tracker_, 0);

  // Verify
  EXPECT_EQ(side_book_->slots[layer].state, OMOrderState::kDead);
  EXPECT_EQ(position_tracker_->get_reserved(), 0);
}

TEST_F(OrderStateManagerTest, HandleCanceled_CancelForReplace_TransitionsToReserved) {
  // Setup: Cancel as part of replace operation
  const int layer = 0;
  const OrderId old_id{12345};

  side_book_->slots[layer].state = OMOrderState::kCancelReserved;
  side_book_->slots[layer].cl_order_id = old_id;
  side_book_->slots[layer].price = PriceType::from_double(100.0);
  side_book_->slots[layer].qty = QtyType::from_double(1.0);

  // Map this order as being replaced
  side_book_->orig_id_to_layer[old_id.value] = layer;

  // Create execution report
  ExecutionReport report;
  report.ord_status = OrdStatus::kCanceled;
  report.cl_order_id = old_id;
  report.price = PriceType::from_double(100.0);
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
  const auto price = PriceType::from_double(100.50);
  const auto qty = QtyType::from_double(2.0);

  side_book_->slots[layer].state = OMOrderState::kReserved;
  side_book_->slots[layer].cl_order_id = order_id;
  side_book_->slots[layer].price = PriceType::from_double(100.50);
  side_book_->slots[layer].qty = QtyType::from_double(2.0);
  side_book_->layer_ticks[layer] = tick_converter_->to_ticks(price.to_double());

  position_tracker_->add_reserved(Side::kBuy, qty.value);

  // Create execution report
  ExecutionReport report;
  report.ord_status = OrdStatus::kRejected;
  report.cl_order_id = order_id;
  report.price = PriceType{price};
  report.side = Side::kBuy;

  // Execute
  state_manager_->handle_execution_report(&report, *side_book_, *position_tracker_, 0);

  // Verify
  EXPECT_EQ(side_book_->slots[layer].state, OMOrderState::kDead);
  EXPECT_EQ(position_tracker_->get_reserved(), 0);
}

TEST_F(OrderStateManagerTest, HandleRejected_ReplaceRejected_RestoresOriginalState) {
  // Setup: Rejected replace operation
  const int layer = 0;
  const OrderId old_id{12345};
  const OrderId new_id{12346};
  const auto old_price = PriceType::from_double(100.0);
  const auto new_price = PriceType::from_double(101.0);
  const auto old_qty = QtyType::from_double(5.0);
  const auto new_qty = QtyType::from_double(7.0);

  side_book_->slots[layer].state = OMOrderState::kCancelReserved;
  side_book_->slots[layer].cl_order_id = new_id;
  side_book_->slots[layer].price = new_price;
  side_book_->slots[layer].qty = new_qty;

  // Set pending replace info
  side_book_->pending_repl[layer] = PendingReplaceInfo(
    new_price,
    new_qty,
    tick_converter_->to_ticks(new_price.to_double()),
    new_id,
    old_qty,
    old_id,
    old_price,
    tick_converter_->to_ticks(old_price.to_double())
  );

  side_book_->new_id_to_layer[new_id.value] = layer;

  const int64_t delta_qty = new_qty.value - old_qty.value;
  position_tracker_->add_reserved(Side::kBuy, old_qty.value);
  position_tracker_->add_reserved(Side::kBuy, delta_qty);

  // Create execution report
  ExecutionReport report;
  report.ord_status = OrdStatus::kRejected;
  report.cl_order_id = new_id;
  report.price = PriceType{new_price};
  report.side = Side::kBuy;

  // Execute
  state_manager_->handle_execution_report(&report, *side_book_, *position_tracker_, 0);

  // Verify: Order should be restored to original state
  EXPECT_EQ(side_book_->slots[layer].state, OMOrderState::kLive);
  EXPECT_EQ(side_book_->slots[layer].cl_order_id, old_id);
  EXPECT_EQ(side_book_->slots[layer].price.value, old_price.value);
  EXPECT_EQ(side_book_->slots[layer].qty.value, old_qty.value);
  EXPECT_FALSE(side_book_->pending_repl[layer].has_value());
  EXPECT_EQ(side_book_->new_id_to_layer.count(new_id.value), 0);
  EXPECT_EQ(position_tracker_->get_reserved(), old_qty.value);
}

TEST_F(OrderStateManagerTest, HandleExpired_TransitionsToDead) {
  // Setup
  const int layer = 0;
  const OrderId order_id{12345};
  const auto price = PriceType::from_double(100.50);
  const auto qty = QtyType::from_double(2.5);

  side_book_->slots[layer].state = OMOrderState::kLive;
  side_book_->slots[layer].cl_order_id = order_id;
  side_book_->slots[layer].price = price;
  side_book_->slots[layer].qty = qty;
  side_book_->layer_ticks[layer] = tick_converter_->to_ticks(price.to_double());

  position_tracker_->add_reserved(Side::kBuy, qty.value);

  // Create execution report
  ExecutionReport report;
  report.ord_status = OrdStatus::kExpired;
  report.cl_order_id = order_id;
  report.price = PriceType::from_double(price.value);
  report.side = Side::kBuy;

  // Execute
  state_manager_->handle_execution_report(&report, *side_book_, *position_tracker_, 0);

  // Verify
  EXPECT_EQ(side_book_->slots[layer].state, OMOrderState::kDead);
  EXPECT_EQ(position_tracker_->get_reserved(), 0);
}
