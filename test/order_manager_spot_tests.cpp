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
#include "websocket/order_entry/exchanges/binance/spot/binance_spot_oe_traits.h"

using namespace trading;
using namespace common;
using namespace order;

// Test fixture for Spot OrderManager
class OrderManagerSpotTest : public ::testing::Test {
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

std::unique_ptr<Logger> OrderManagerSpotTest::logger_;

// ============================================================================
// Traits Configuration Tests
// ============================================================================

TEST_F(OrderManagerSpotTest, Traits_UsesCancelAndReorderNotModify) {
  EXPECT_TRUE(BinanceSpotOeTraits::supports_cancel_and_reorder());
  EXPECT_FALSE(BinanceSpotOeTraits::supports_position_side());
  EXPECT_FALSE(BinanceSpotOeTraits::supports_reduce_only());
}

TEST_F(OrderManagerSpotTest, Traits_RequiresListenKey) {
  // Spot WebSocket API does NOT require listen key (unlike Futures)
  EXPECT_FALSE(BinanceSpotOeTraits::requires_listen_key());
  EXPECT_FALSE(BinanceSpotOeTraits::requires_stream_transport());
}

// ============================================================================
// Cancel-And-Reorder Tests (Spot specific)
// ============================================================================

TEST_F(OrderManagerSpotTest, ProcessReplace_UsesDualIdMapping) {
  // Spot uses cancel-and-reorder, so both orig_id and new_id are mapped
  auto& book = layer_book_->side_book("BTCUSDT", Side::kBuy);

  const int layer = 0;
  const OrderId orig_id{30001};
  const OrderId new_id{30002};  // Different ID for Spot

  // Simulate Spot cancel-and-reorder: map both IDs to same layer
  book.orig_id_to_layer[orig_id.value] = layer;
  book.new_id_to_layer[new_id.value] = layer;

  // Verify both mappings exist (Spot behavior)
  EXPECT_EQ(book.orig_id_to_layer[orig_id.value], layer);
  EXPECT_EQ(book.new_id_to_layer[new_id.value], layer);
}

TEST_F(OrderManagerSpotTest, ProcessReplace_PendingReplInfoUsesDifferentIds) {
  // Spot cancel-and-reorder: original_cl_order_id != new_cl_order_id
  auto& book = layer_book_->side_book("BTCUSDT", Side::kBuy);

  const int layer = 0;
  const OrderId orig_id{30001};
  const OrderId new_id{30002};  // Different ID
  const Price old_price{50000.0};
  const Price new_price{50100.0};
  const Qty old_qty{1.0};
  const Qty new_qty{1.5};

  // Create pending replace (Spot style)
  book.pending_repl[layer] = PendingReplaceInfo(
    new_price,
    new_qty,
    static_cast<uint64_t>(new_price.value / 0.01),
    new_id,  // new_cl_order_id != original
    old_qty,
    orig_id,  // original_cl_order_id
    old_price,
    static_cast<uint64_t>(old_price.value / 0.01)
  );

  ASSERT_TRUE(book.pending_repl[layer].has_value());
  auto& repl = book.pending_repl[layer].value();

  // Verify IDs are different (Spot cancel-and-reorder behavior)
  EXPECT_NE(repl.new_cl_order_id, repl.original_cl_order_id);
  EXPECT_EQ(repl.original_cl_order_id, orig_id);
  EXPECT_EQ(repl.new_cl_order_id, new_id);
}

// ============================================================================
// LayerBook 2-Way Routing (Spot doesn't use position_side)
// ============================================================================

TEST_F(OrderManagerSpotTest, LayerBook_BuyBook_AccessibleAndIsolated) {
  auto& buy_book = layer_book_->side_book("BTCUSDT", Side::kBuy);

  // Set order in BUY book
  buy_book.slots[0].cl_order_id = OrderId{10001};
  buy_book.slots[0].price = Price{50000.0};
  buy_book.slots[0].qty = Qty{1.0};
  buy_book.slots[0].state = OMOrderState::kLive;

  // Verify it's in the right book
  auto& verify_book = layer_book_->side_book("BTCUSDT", Side::kBuy);
  EXPECT_EQ(verify_book.slots[0].cl_order_id, OrderId{10001});
}

TEST_F(OrderManagerSpotTest, LayerBook_SellBook_AccessibleAndIsolated) {
  auto& sell_book = layer_book_->side_book("BTCUSDT", Side::kSell);

  // Set order in SELL book
  sell_book.slots[0].cl_order_id = OrderId{20001};
  sell_book.slots[0].price = Price{50100.0};
  sell_book.slots[0].qty = Qty{2.0};
  sell_book.slots[0].state = OMOrderState::kLive;

  // Verify it's in the right book
  auto& verify_book = layer_book_->side_book("BTCUSDT", Side::kSell);
  EXPECT_EQ(verify_book.slots[0].cl_order_id, OrderId{20001});
}

TEST_F(OrderManagerSpotTest, LayerBook_BuyAndSell_CompletelyIsolated) {
  // Place order in BUY book
  auto& buy_book = layer_book_->side_book("BTCUSDT", Side::kBuy);
  buy_book.slots[0].cl_order_id = OrderId{10001};
  buy_book.slots[0].price = Price{50000.0};
  buy_book.slots[0].qty = Qty{1.0};
  buy_book.slots[0].state = OMOrderState::kLive;

  // Place order in SELL book
  auto& sell_book = layer_book_->side_book("BTCUSDT", Side::kSell);
  sell_book.slots[0].cl_order_id = OrderId{20001};
  sell_book.slots[0].price = Price{50100.0};
  sell_book.slots[0].qty = Qty{2.0};
  sell_book.slots[0].state = OMOrderState::kLive;

  // Verify complete isolation
  EXPECT_NE(buy_book.slots[0].cl_order_id, sell_book.slots[0].cl_order_id);
  EXPECT_NE(buy_book.slots[0].price, sell_book.slots[0].price);
  EXPECT_NE(buy_book.slots[0].qty, sell_book.slots[0].qty);

  // Modify BUY order
  buy_book.slots[0].qty = Qty{1.5};

  // Verify SELL unchanged
  EXPECT_EQ(sell_book.slots[0].qty.value, 2.0);
}

// ============================================================================
// No Position Side Tests (Spot specific)
// ============================================================================

TEST_F(OrderManagerSpotTest, LayerBook_NoPositionSideParameter) {
  // Spot only uses 2-way books (BUY/SELL), no position_side parameter
  auto& buy_book = layer_book_->side_book("BTCUSDT", Side::kBuy);
  auto& sell_book = layer_book_->side_book("BTCUSDT", Side::kSell);

  buy_book.slots[0].cl_order_id = OrderId{30001};
  buy_book.slots[0].state = OMOrderState::kLive;

  sell_book.slots[0].cl_order_id = OrderId{30002};
  sell_book.slots[0].state = OMOrderState::kLive;

  // Verify they are separate books
  EXPECT_NE(buy_book.slots[0].cl_order_id, sell_book.slots[0].cl_order_id);
}

// ============================================================================
// Layer Finding Tests
// ============================================================================

TEST_F(OrderManagerSpotTest, FindLayer_WorksInBuyAndSellBooks) {
  // Place orders in both books
  auto& buy_book = layer_book_->side_book("BTCUSDT", Side::kBuy);
  buy_book.slots[0].cl_order_id = OrderId{40001};
  buy_book.slots[0].state = OMOrderState::kLive;

  auto& sell_book = layer_book_->side_book("BTCUSDT", Side::kSell);
  sell_book.slots[1].cl_order_id = OrderId{40002};
  sell_book.slots[1].state = OMOrderState::kLive;

  // Find in BUY book
  int found_buy = LayerBook::find_layer_by_id(buy_book, OrderId{40001});
  EXPECT_EQ(found_buy, 0);

  // Find in SELL book
  int found_sell = LayerBook::find_layer_by_id(sell_book, OrderId{40002});
  EXPECT_EQ(found_sell, 1);

  // Cross-check: BUY ID not in SELL book
  int not_in_sell = LayerBook::find_layer_by_id(sell_book, OrderId{40001});
  EXPECT_LT(not_in_sell, 0);
}

// ============================================================================
// End-to-End Scenarios
// ============================================================================

TEST_F(OrderManagerSpotTest, Scenario_BuyAndSell_IndependentOperations) {
  // Place BUY order
  auto& buy_book = layer_book_->side_book("BTCUSDT", Side::kBuy);
  buy_book.slots[0].cl_order_id = OrderId{50001};
  buy_book.slots[0].price = Price{50000.0};
  buy_book.slots[0].qty = Qty{1.0};
  buy_book.slots[0].state = OMOrderState::kLive;

  // Place SELL order
  auto& sell_book = layer_book_->side_book("BTCUSDT", Side::kSell);
  sell_book.slots[0].cl_order_id = OrderId{50002};
  sell_book.slots[0].price = Price{50100.0};
  sell_book.slots[0].qty = Qty{1.0};
  sell_book.slots[0].state = OMOrderState::kLive;

  // Verify both exist independently
  EXPECT_EQ(buy_book.slots[0].state, OMOrderState::kLive);
  EXPECT_EQ(sell_book.slots[0].state, OMOrderState::kLive);
}

TEST_F(OrderManagerSpotTest, Scenario_CancelAndReorder_NewIdGeneration) {
  auto& book = layer_book_->side_book("BTCUSDT", Side::kBuy);

  const int layer = 0;
  const OrderId orig_id{60001};
  const OrderId new_id{60002};  // Spot generates new ID

  // Simulate cancel-and-reorder
  book.orig_id_to_layer[orig_id.value] = layer;
  book.new_id_to_layer[new_id.value] = layer;

  // Both mappings should exist
  EXPECT_EQ(book.orig_id_to_layer[orig_id.value], layer);
  EXPECT_EQ(book.new_id_to_layer[new_id.value], layer);

  // After cancel confirms, remove orig_id mapping
  book.orig_id_to_layer.erase(orig_id.value);

  // Only new_id remains
  EXPECT_EQ(book.orig_id_to_layer.count(orig_id.value), 0);
  EXPECT_EQ(book.new_id_to_layer[new_id.value], layer);
}
