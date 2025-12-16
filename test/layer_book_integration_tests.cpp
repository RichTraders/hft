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
#include "layer_book.h"
#include "orders.h"
#include "logger.h"

using namespace trading;
using namespace common;
using namespace order;

class LayerBookIntegrationTest : public ::testing::Test {
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

std::unique_ptr<Logger> LayerBookIntegrationTest::logger_;

// ============================================================================
// Futures 4-Way Position Isolation Tests
// ============================================================================

TEST_F(LayerBookIntegrationTest, Futures_LongAndShortPositions_AreIsolated) {
  // Place LONG BUY order
  auto& long_buy = layer_book_->side_book("BTCUSDT", Side::kBuy, PositionSide::kLong);
  long_buy.slots[0].cl_order_id = OrderId{10001};
  long_buy.slots[0].price = Price{50000.00};
  long_buy.slots[0].qty = Qty{1.0};
  long_buy.slots[0].state = OMOrderState::kLive;
  long_buy.layer_ticks[0] = static_cast<uint64_t>(50000.00 / 0.01);

  // Place SHORT SELL order (different position)
  auto& short_sell = layer_book_->side_book("BTCUSDT", Side::kSell, PositionSide::kShort);
  short_sell.slots[0].cl_order_id = OrderId{20001};
  short_sell.slots[0].price = Price{50100.00};
  short_sell.slots[0].qty = Qty{2.0};
  short_sell.slots[0].state = OMOrderState::kLive;
  short_sell.layer_ticks[0] = static_cast<uint64_t>(50100.00 / 0.01);

  // Verify they are completely isolated
  EXPECT_NE(long_buy.slots[0].cl_order_id, short_sell.slots[0].cl_order_id);
  EXPECT_NE(long_buy.slots[0].price.value, short_sell.slots[0].price.value);
  EXPECT_NE(long_buy.slots[0].qty.value, short_sell.slots[0].qty.value);

  // Modify LONG position
  long_buy.slots[0].qty = Qty{1.5};

  // Verify SHORT position is unaffected
  EXPECT_EQ(short_sell.slots[0].qty.value, 2.0);
}

TEST_F(LayerBookIntegrationTest, Futures_LongExitAndShortExit_UseDifferentBooks) {
  // LONG exit (SELL)
  auto& long_sell = layer_book_->side_book("BTCUSDT", Side::kSell, PositionSide::kLong);
  long_sell.slots[0].cl_order_id = OrderId{10002};
  long_sell.slots[0].price = Price{50200.00};
  long_sell.slots[0].qty = Qty{1.0};
  long_sell.slots[0].state = OMOrderState::kLive;

  // SHORT exit (BUY)
  auto& short_buy = layer_book_->side_book("BTCUSDT", Side::kBuy, PositionSide::kShort);
  short_buy.slots[0].cl_order_id = OrderId{20002};
  short_buy.slots[0].price = Price{49800.00};
  short_buy.slots[0].qty = Qty{2.0};
  short_buy.slots[0].state = OMOrderState::kLive;

  // They should be in different books
  EXPECT_NE(long_sell.slots[0].cl_order_id, short_buy.slots[0].cl_order_id);
}

// ============================================================================
// Spot vs Futures Book Access Tests
// ============================================================================

TEST_F(LayerBookIntegrationTest, Spot_UsesOnlyBuyAndSellBooks) {
  // Spot doesn't use position_side
  auto& buy_book = layer_book_->side_book("BTCUSDT", Side::kBuy);
  auto& sell_book = layer_book_->side_book("BTCUSDT", Side::kSell);

  buy_book.slots[0].cl_order_id = OrderId{30001};
  buy_book.slots[0].state = OMOrderState::kLive;

  sell_book.slots[0].cl_order_id = OrderId{30002};
  sell_book.slots[0].state = OMOrderState::kLive;

  EXPECT_NE(buy_book.slots[0].cl_order_id, sell_book.slots[0].cl_order_id);
}

// ============================================================================
// Layer Mapping Tests
// ============================================================================

TEST_F(LayerBookIntegrationTest, FindLayerById_WorksAcrossMultipleLayers) {
  auto& book = layer_book_->side_book("BTCUSDT", Side::kBuy);

  // Fill multiple layers
  for (int i = 0; i < 5; i++) {
    book.slots[i].cl_order_id = OrderId{static_cast<uint64_t>(40000 + i)};
    book.slots[i].state = OMOrderState::kLive;
  }

  // Find each one
  for (int i = 0; i < 5; i++) {
    int found = LayerBook::find_layer_by_id(book, OrderId{static_cast<uint64_t>(40000 + i)});
    EXPECT_EQ(found, i);
  }

  // Not found
  int not_found = LayerBook::find_layer_by_id(book, OrderId{99999});
  EXPECT_LT(not_found, 0);
}

TEST_F(LayerBookIntegrationTest, FindLayerByTicks_WorksForMultiplePriceLevels) {
  auto& book = layer_book_->side_book("BTCUSDT", Side::kBuy);

  // Place orders at different price levels
  double prices[] = {50000.00, 50010.00, 50020.00, 50030.00};
  for (int i = 0; i < 4; i++) {
    uint64_t tick = static_cast<uint64_t>(prices[i] / 0.01);
    book.layer_ticks[i] = tick;
    book.slots[i].state = OMOrderState::kLive;
  }

  // Find by ticks
  for (int i = 0; i < 4; i++) {
    uint64_t tick = static_cast<uint64_t>(prices[i] / 0.01);
    int found = LayerBook::find_layer_by_ticks(book, tick);
    EXPECT_EQ(found, i);
  }
}

// ============================================================================
// Pending Replace Tracking Tests
// ============================================================================

TEST_F(LayerBookIntegrationTest, PendingReplace_TracksOriginalAndNewState) {
  auto& book = layer_book_->side_book("BTCUSDT", Side::kBuy);

  const int layer = 0;
  const OrderId orig_id{50001};
  const OrderId new_id{50002};
  const Price orig_price{50000.00};
  const Price new_price{50100.00};
  const Qty orig_qty{1.0};
  const Qty new_qty{1.5};

  // Set original state
  book.slots[layer].cl_order_id = orig_id;
  book.slots[layer].price = orig_price;
  book.slots[layer].qty = orig_qty;
  book.slots[layer].state = OMOrderState::kLive;

  // Create pending replace
  book.pending_repl[layer] = PendingReplaceInfo(
    new_price,
    new_qty,
    static_cast<uint64_t>(new_price.value / 0.01),
    new_id,
    orig_qty,
    orig_id,
    orig_price,
    static_cast<uint64_t>(orig_price.value / 0.01)
  );

  // Verify pending replace info
  ASSERT_TRUE(book.pending_repl[layer].has_value());
  auto& repl = book.pending_repl[layer].value();

  EXPECT_EQ(repl.original_cl_order_id, orig_id);
  EXPECT_EQ(repl.new_cl_order_id, new_id);
  EXPECT_EQ(repl.original_price, orig_price);
  EXPECT_EQ(repl.new_price, new_price);
  EXPECT_EQ(repl.last_qty, orig_qty);
  EXPECT_EQ(repl.new_qty, new_qty);
}

// ============================================================================
// ID Mapping Tests
// ============================================================================

TEST_F(LayerBookIntegrationTest, IdMapping_OrigAndNewIds_MappedIndependently) {
  auto& book = layer_book_->side_book("BTCUSDT", Side::kBuy);

  const int layer = 2;
  const OrderId orig_id{60001};
  const OrderId new_id{60002};

  // Map both IDs to same layer (for replace operation)
  book.orig_id_to_layer[orig_id.value] = layer;
  book.new_id_to_layer[new_id.value] = layer;

  // Verify both mappings
  EXPECT_EQ(book.orig_id_to_layer[orig_id.value], layer);
  EXPECT_EQ(book.new_id_to_layer[new_id.value], layer);

  // Clear original ID mapping (after cancel confirms)
  book.orig_id_to_layer.erase(orig_id.value);

  EXPECT_EQ(book.orig_id_to_layer.count(orig_id.value), 0);
  EXPECT_EQ(book.new_id_to_layer[new_id.value], layer);  // New ID still mapped
}

// ============================================================================
// Unmap Layer Tests
// ============================================================================

TEST_F(LayerBookIntegrationTest, UnmapLayer_ClearsAllAssociatedData) {
  auto& book = layer_book_->side_book("BTCUSDT", Side::kBuy);

  const int layer = 1;
  const OrderId order_id{70001};
  const uint64_t tick = static_cast<uint64_t>(50000.00 / 0.01);

  // Setup complete layer state
  book.slots[layer].cl_order_id = order_id;
  book.slots[layer].state = OMOrderState::kLive;
  book.slots[layer].price = Price{50000.00};
  book.layer_ticks[layer] = tick;
  book.new_id_to_layer[order_id.value] = layer;
  book.orig_id_to_layer[order_id.value] = layer;

  // Unmap
  LayerBook::unmap_layer(book, layer);

  // Verify all cleared
  EXPECT_EQ(book.layer_ticks[layer], 0);
  EXPECT_EQ(book.new_id_to_layer.count(order_id.value), 0);
  EXPECT_EQ(book.orig_id_to_layer.count(order_id.value), 0);
}

// ============================================================================
// Find Free Layer Tests
// ============================================================================

TEST_F(LayerBookIntegrationTest, FindFreeLayer_ReturnsFirstAvailable) {
  auto& book = layer_book_->side_book("BTCUSDT", Side::kBuy);

  // Fill first 3 layers
  for (int i = 0; i < 3; i++) {
    book.slots[i].state = OMOrderState::kLive;
    book.layer_ticks[i] = static_cast<uint64_t>((50000 + i * 10) / 0.01);
  }

  // Find free should return layer 3 (first non-kLive)
  int free_layer = LayerBook::find_free_layer(book);
  EXPECT_EQ(free_layer, 3);
}

TEST_F(LayerBookIntegrationTest, FindFreeLayer_ReturnsFirstDeadOrInvalid) {
  auto& book = layer_book_->side_book("BTCUSDT", Side::kBuy);

  // Set states: kLive, kDead, kInvalid
  book.slots[0].state = OMOrderState::kLive;
  book.layer_ticks[0] = static_cast<uint64_t>(50000.00 / 0.01);
  book.slots[1].state = OMOrderState::kDead;  // Should return this
  book.slots[2].state = OMOrderState::kInvalid;

  int free_layer = LayerBook::find_free_layer(book);
  EXPECT_EQ(free_layer, 1);  // Returns first kDead or kInvalid
}

// ============================================================================
// Pick Victim Layer Tests
// ============================================================================

TEST_F(LayerBookIntegrationTest, PickVictimLayer_SelectsLeastRecentlyUsed) {
  auto& book = layer_book_->side_book("BTCUSDT", Side::kBuy);

  // Initialize all 8 slots with high last_used values
  for (int i = 0; i < 8; i++) {
    book.slots[i].state = OMOrderState::kLive;
    book.layer_ticks[i] = static_cast<uint64_t>((50000 + i * 10) / 0.01);
    book.slots[i].last_used = 10000 + i;  // Set high baseline
  }

  // Now set specific last_used times for first 5 slots
  book.slots[0].last_used = 1000;
  book.slots[1].last_used = 500;   // Oldest among first 5, should be victim
  book.slots[2].last_used = 2000;
  book.slots[3].last_used = 1500;
  book.slots[4].last_used = 1200;

  int victim = LayerBook::pick_victim_layer(book);
  EXPECT_EQ(victim, 1);
}
