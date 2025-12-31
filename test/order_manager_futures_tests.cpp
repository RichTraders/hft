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

#include "layer_book.h"
#include "orders.h"
#include "logger.h"
#include "websocket/order_entry/exchanges/binance/futures/binance_futures_oe_traits.h"

using namespace trading;
using namespace common;
using namespace order;

// Direct LayerBook tests that verify Futures-specific behavior
class OrderManagerFuturesTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    logger_ = std::make_unique<Logger>();
    logger_->setLevel(LogLevel::kDebug);
    logger_->clearSink();
  }

  static void TearDownTestSuite() {
    logger_->shutdown();
    logger_.reset();
  }

  void SetUp() override {
    layer_book_ = std::make_unique<LayerBook>("BTCUSDT");
  }

  void TearDown() override {
    layer_book_.reset();
  }

  static std::unique_ptr<Logger> logger_;
  std::unique_ptr<LayerBook> layer_book_;
};

std::unique_ptr<Logger> OrderManagerFuturesTest::logger_;

// ============================================================================
// Traits Configuration Tests
// ============================================================================

TEST_F(OrderManagerFuturesTest, Traits_SupportsModifyNotCancelAndReorder) {
  EXPECT_FALSE(BinanceFuturesOeTraits::supports_cancel_and_reorder());
  EXPECT_TRUE(BinanceFuturesOeTraits::supports_position_side());
  EXPECT_TRUE(BinanceFuturesOeTraits::supports_reduce_only());
}

TEST_F(OrderManagerFuturesTest, Traits_RequiresListenKey) {
  EXPECT_TRUE(BinanceFuturesOeTraits::requires_listen_key());
  EXPECT_TRUE(BinanceFuturesOeTraits::requires_stream_transport());
}

// ============================================================================
// Position Side - LayerBook 4-Way Routing
// ============================================================================

TEST_F(OrderManagerFuturesTest, LayerBook_LongBuy_AccessibleAndIsolated) {
  auto& long_buy = layer_book_->side_book("BTCUSDT", Side::kBuy, PositionSide::kLong);

  // Set order in LONG BUY book
  long_buy.slots[0].cl_order_id = OrderId{10001};
  long_buy.slots[0].price = PriceType::from_double(50000.0);
  long_buy.slots[0].qty = QtyType::from_double(1.0);
  long_buy.slots[0].state = OMOrderState::kLive;

  // Verify it's in the right book
  auto& verify_book = layer_book_->side_book("BTCUSDT", Side::kBuy, PositionSide::kLong);
  EXPECT_EQ(verify_book.slots[0].cl_order_id, OrderId{10001});
}

TEST_F(OrderManagerFuturesTest, LayerBook_LongSell_ExitPosition) {
  auto& long_sell = layer_book_->side_book("BTCUSDT", Side::kSell, PositionSide::kLong);

  // LONG exit order (sell to close long position)
  long_sell.slots[0].cl_order_id = OrderId{10002};
  long_sell.slots[0].price = PriceType::from_double(50100.0);
  long_sell.slots[0].qty = QtyType::from_double(1.0);
  long_sell.slots[0].state = OMOrderState::kLive;

  auto& verify_book = layer_book_->side_book("BTCUSDT", Side::kSell, PositionSide::kLong);
  EXPECT_EQ(verify_book.slots[0].cl_order_id, OrderId{10002});
}

TEST_F(OrderManagerFuturesTest, LayerBook_ShortSell_EntryPosition) {
  auto& short_sell = layer_book_->side_book("BTCUSDT", Side::kSell, PositionSide::kShort);

  // SHORT entry order (sell to open short position)
  short_sell.slots[0].cl_order_id = OrderId{20001};
  short_sell.slots[0].price = PriceType::from_double(50000.0);
  short_sell.slots[0].qty = QtyType::from_double(2.0);
  short_sell.slots[0].state = OMOrderState::kLive;

  auto& verify_book = layer_book_->side_book("BTCUSDT", Side::kSell, PositionSide::kShort);
  EXPECT_EQ(verify_book.slots[0].cl_order_id, OrderId{20001});
}

TEST_F(OrderManagerFuturesTest, LayerBook_ShortBuy_ExitPosition) {
  auto& short_buy = layer_book_->side_book("BTCUSDT", Side::kBuy, PositionSide::kShort);

  // SHORT exit order (buy to close short position)
  short_buy.slots[0].cl_order_id = OrderId{20002};
  short_buy.slots[0].price = PriceType::from_double(49900.0);
  short_buy.slots[0].qty = QtyType::from_double(2.0);
  short_buy.slots[0].state = OMOrderState::kLive;

  auto& verify_book = layer_book_->side_book("BTCUSDT", Side::kBuy, PositionSide::kShort);
  EXPECT_EQ(verify_book.slots[0].cl_order_id, OrderId{20002});
}

TEST_F(OrderManagerFuturesTest, LayerBook_LongAndShort_CompletelyIsolated) {
  // Place order in LONG BUY
  auto& long_buy = layer_book_->side_book("BTCUSDT", Side::kBuy, PositionSide::kLong);
  long_buy.slots[0].cl_order_id = OrderId{10001};
  long_buy.slots[0].price = PriceType::from_double(50000.0);
  long_buy.slots[0].qty = QtyType::from_double(1.0);
  long_buy.slots[0].state = OMOrderState::kLive;

  // Place order in SHORT BUY (different position, same side)
  auto& short_buy = layer_book_->side_book("BTCUSDT", Side::kBuy, PositionSide::kShort);
  short_buy.slots[0].cl_order_id = OrderId{20001};
  short_buy.slots[0].price = PriceType::from_double(49900.0);
  short_buy.slots[0].qty = QtyType::from_double(2.0);
  short_buy.slots[0].state = OMOrderState::kLive;

  // Verify complete isolation
  EXPECT_NE(long_buy.slots[0].cl_order_id, short_buy.slots[0].cl_order_id);
  EXPECT_NE(long_buy.slots[0].price.value, short_buy.slots[0].price.value);
  EXPECT_NE(long_buy.slots[0].qty.value, short_buy.slots[0].qty.value);

  // Modify LONG position
  long_buy.slots[0].qty = QtyType::from_double(1.5);

  // Verify SHORT unchanged
  EXPECT_EQ(short_buy.slots[0].qty.to_double(), 2.0);
}

// ============================================================================
// Modify API (Futures uses order.modify, not cancel-and-reorder)
// ============================================================================

TEST_F(OrderManagerFuturesTest, ProcessReplace_UsesSingleIdMapping) {
  // In Futures modify, we reuse the same order ID
  // So only new_id_to_layer is populated (with original ID)
  auto& book = layer_book_->side_book("BTCUSDT", Side::kBuy, PositionSide::kLong);

  const int layer = 0;
  const OrderId orig_id{30001};

  // Simulate Futures modify: map original ID as "new" ID
  book.new_id_to_layer[orig_id.value] = layer;

  // In Futures, orig_id_to_layer should NOT be used for modify
  EXPECT_EQ(book.orig_id_to_layer.count(orig_id.value), 0);
  EXPECT_EQ(book.new_id_to_layer[orig_id.value], layer);
}

TEST_F(OrderManagerFuturesTest, ProcessReplace_PendingReplInfoUsesSameId) {
  // Futures modify: both original and new ID are the same
  auto& book = layer_book_->side_book("BTCUSDT", Side::kBuy, PositionSide::kLong);

  const int layer = 0;
  const OrderId order_id{30001};
  const auto old_price = PriceType::from_double(50000.0);
  const auto new_price = PriceType::from_double(50100.0);
  const auto old_qty = QtyType::from_double(1.0);
  const auto new_qty = QtyType::from_double(1.5);

  // Create pending replace (Futures style)
  book.pending_repl[layer] = PendingReplaceInfo(
    new_price,
    new_qty,
    static_cast<uint64_t>(new_price.to_double() / 0.01),
    order_id,  // new_cl_order_id = same as original
    old_qty,
    order_id,  // original_cl_order_id
    old_price,
    static_cast<uint64_t>(old_price.to_double() / 0.01)
  );

  ASSERT_TRUE(book.pending_repl[layer].has_value());
  auto& repl = book.pending_repl[layer].value();

  // Verify both IDs are the same (Futures modify behavior)
  EXPECT_EQ(repl.new_cl_order_id, repl.original_cl_order_id);
  EXPECT_EQ(repl.new_cl_order_id, order_id);
}

// ============================================================================
// Position Side Propagation Tests
// ============================================================================

TEST_F(OrderManagerFuturesTest, NewOrder_PositionSideLong_RoutedCorrectly) {
  auto& book = layer_book_->side_book("BTCUSDT", Side::kBuy, PositionSide::kLong);

  book.slots[0].cl_order_id = OrderId{40001};
  book.slots[0].state = OMOrderState::kLive;

  // Verify we can access this order with LONG position_side
  auto& verify = layer_book_->side_book("BTCUSDT", Side::kBuy, PositionSide::kLong);
  EXPECT_EQ(verify.slots[0].cl_order_id, OrderId{40001});

  // Verify SHORT book is separate
  auto& short_book = layer_book_->side_book("BTCUSDT", Side::kBuy, PositionSide::kShort);
  EXPECT_NE(short_book.slots[0].cl_order_id, OrderId{40001});
}

TEST_F(OrderManagerFuturesTest, NewOrder_PositionSideShort_RoutedCorrectly) {
  auto& book = layer_book_->side_book("BTCUSDT", Side::kSell, PositionSide::kShort);

  book.slots[0].cl_order_id = OrderId{40002};
  book.slots[0].state = OMOrderState::kLive;

  auto& verify = layer_book_->side_book("BTCUSDT", Side::kSell, PositionSide::kShort);
  EXPECT_EQ(verify.slots[0].cl_order_id, OrderId{40002});

  // Verify LONG book is separate
  auto& long_book = layer_book_->side_book("BTCUSDT", Side::kSell, PositionSide::kLong);
  EXPECT_NE(long_book.slots[0].cl_order_id, OrderId{40002});
}

// ============================================================================
// Layer Finding and Mapping Tests
// ============================================================================

TEST_F(OrderManagerFuturesTest, FindLayer_WorksAcrossPositionSides) {
  // Place orders in both LONG and SHORT
  auto& long_book = layer_book_->side_book("BTCUSDT", Side::kBuy, PositionSide::kLong);
  long_book.slots[0].cl_order_id = OrderId{50001};
  long_book.slots[0].state = OMOrderState::kLive;

  auto& short_book = layer_book_->side_book("BTCUSDT", Side::kBuy, PositionSide::kShort);
  short_book.slots[1].cl_order_id = OrderId{50002};
  short_book.slots[1].state = OMOrderState::kLive;

  // Find in LONG book
  int found_long = LayerBook::find_layer_by_id(long_book, OrderId{50001});
  EXPECT_EQ(found_long, 0);

  // Find in SHORT book
  int found_short = LayerBook::find_layer_by_id(short_book, OrderId{50002});
  EXPECT_EQ(found_short, 1);

  // Cross-check: LONG ID not in SHORT book
  int not_in_short = LayerBook::find_layer_by_id(short_book, OrderId{50001});
  EXPECT_LT(not_in_short, 0);
}

// ============================================================================
// End-to-End Scenarios
// ============================================================================

TEST_F(OrderManagerFuturesTest, Scenario_LongPosition_EntryAndExit) {
  // Entry: LONG BUY
  auto& entry_book = layer_book_->side_book("BTCUSDT", Side::kBuy, PositionSide::kLong);
  entry_book.slots[0].cl_order_id = OrderId{60001};
  entry_book.slots[0].price = PriceType::from_double(50000.0);
  entry_book.slots[0].qty = QtyType::from_double(1.0);
  entry_book.slots[0].state = OMOrderState::kLive;

  // Exit: LONG SELL
  auto& exit_book = layer_book_->side_book("BTCUSDT", Side::kSell, PositionSide::kLong);
  exit_book.slots[0].cl_order_id = OrderId{60002};
  exit_book.slots[0].price = PriceType::from_double(50500.0);
  exit_book.slots[0].qty = QtyType::from_double(1.0);
  exit_book.slots[0].state = OMOrderState::kLive;

  // Verify both exist independently
  EXPECT_EQ(entry_book.slots[0].state, OMOrderState::kLive);
  EXPECT_EQ(exit_book.slots[0].state, OMOrderState::kLive);
}

TEST_F(OrderManagerFuturesTest, Scenario_ShortPosition_EntryAndExit) {
  // Entry: SHORT SELL
  auto& entry_book = layer_book_->side_book("BTCUSDT", Side::kSell, PositionSide::kShort);
  entry_book.slots[0].cl_order_id = OrderId{60003};
  entry_book.slots[0].price = PriceType::from_double(50000.0);
  entry_book.slots[0].qty = QtyType::from_double(2.0);
  entry_book.slots[0].state = OMOrderState::kLive;

  // Exit: SHORT BUY
  auto& exit_book = layer_book_->side_book("BTCUSDT", Side::kBuy, PositionSide::kShort);
  exit_book.slots[0].cl_order_id = OrderId{60004};
  exit_book.slots[0].price = PriceType::from_double(49500.0);
  exit_book.slots[0].qty = QtyType::from_double(2.0);
  exit_book.slots[0].state = OMOrderState::kLive;

  EXPECT_EQ(entry_book.slots[0].state, OMOrderState::kLive);
  EXPECT_EQ(exit_book.slots[0].state, OMOrderState::kLive);
}

TEST_F(OrderManagerFuturesTest, Scenario_SimultaneousLongAndShort) {
  // LONG position: BUY entry
  auto& long_buy = layer_book_->side_book("BTCUSDT", Side::kBuy, PositionSide::kLong);
  long_buy.slots[0].cl_order_id = OrderId{70001};
  long_buy.slots[0].qty = QtyType::from_double(1.0);
  long_buy.slots[0].state = OMOrderState::kLive;

  // SHORT position: SELL entry (at same time)
  auto& short_sell = layer_book_->side_book("BTCUSDT", Side::kSell, PositionSide::kShort);
  short_sell.slots[0].cl_order_id = OrderId{70002};
  short_sell.slots[0].qty = QtyType::from_double(1.0);
  short_sell.slots[0].state = OMOrderState::kLive;

  // Both should coexist
  EXPECT_EQ(long_buy.slots[0].state, OMOrderState::kLive);
  EXPECT_EQ(short_sell.slots[0].state, OMOrderState::kLive);

  // Modify LONG
  long_buy.slots[0].qty = QtyType::from_double(1.5);

  // SHORT unaffected
  EXPECT_EQ(short_sell.slots[0].qty.to_double(), 1.0);
}
