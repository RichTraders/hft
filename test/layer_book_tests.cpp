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

using trading::kSlotsPerSide;
using trading::kTicksInvalid;
using trading::order::LayerBook;
using trading::order::OrderSlot;
using trading::order::SideBook;

using namespace trading;

const common::TickerId kSym{"TEST"};
constexpr uint64_t tick(uint64_t t) {
  return t;
}

inline common::OrderId mkId(uint64_t v) {
  return common::OrderId{v};
}

TEST(LayerBookTest, InitializationAndSideBooks) {
  LayerBook lb{kSym};
  auto& buy = lb.side_book(kSym, common::Side::kBuy);
  auto& sell = lb.side_book(kSym, common::Side::kSell);

  for (int i = 0; i < kSlotsPerSide; ++i) {
    EXPECT_EQ(buy.layer_ticks[i], kTicksInvalid);
    EXPECT_EQ(sell.layer_ticks[i], kTicksInvalid);
    EXPECT_EQ(buy.slots[i].state, OMOrderState::kInvalid);
    EXPECT_EQ(sell.slots[i].state, OMOrderState::kInvalid);
  }
}

TEST(LayerBookTest, FindAndAssignExistingThenUpdateLastUsed) {
  LayerBook lb{kSym};
  auto& sb = lb.side_book(kSym, common::Side::kBuy);

  auto a1 = LayerBook::get_or_assign_layer(sb, tick(100), /*now=*/10);
  ASSERT_GE(a1.layer, 0);
  EXPECT_FALSE(a1.victim_live_layer.has_value());
  EXPECT_EQ(sb.layer_ticks[a1.layer], tick(100));
  EXPECT_EQ(sb.slots[a1.layer].last_used, 10u);

  auto a2 = LayerBook::get_or_assign_layer(sb, tick(100), /*now=*/20);
  EXPECT_EQ(a2.layer, a1.layer);
  EXPECT_FALSE(a2.victim_live_layer.has_value());
  EXPECT_EQ(sb.slots[a1.layer].last_used, 20u);

  EXPECT_EQ(LayerBook::find_layer_by_ticks(sb, tick(100)), a1.layer);
}

TEST(LayerBookTest, FindLayerById) {
  LayerBook lb{kSym};
  auto& sb = lb.side_book(kSym, common::Side::kBuy);

  const int layer = 2;
  sb.slots[layer].cl_order_id = mkId(42);
  sb.slots[layer].state = OMOrderState::kLive;

  EXPECT_EQ(LayerBook::find_layer_by_id(sb, mkId(42)), layer);
  EXPECT_EQ(
      LayerBook::find_layer_by_id(sb, common::OrderId{common::kOrderIdInvalid}),
      -1);
  EXPECT_EQ(LayerBook::find_layer_by_id(sb, mkId(999)), -1);
}

TEST(LayerBookTest, FindFreeLayerPrefersInvalidDeadOrUnmapped) {
  LayerBook lb{kSym};
  auto& sb = lb.side_book(kSym, common::Side::kBuy);

  EXPECT_EQ(LayerBook::find_free_layer(sb), 0);

  sb.slots[0].state = OMOrderState::kLive;
  sb.layer_ticks[0] = tick(100);
  EXPECT_EQ(LayerBook::find_free_layer(sb), 1);

  sb.layer_ticks[1] = tick(101);
  EXPECT_EQ(LayerBook::find_free_layer(sb), 1);

  sb.slots[1].state = OMOrderState::kLive;
  EXPECT_EQ(LayerBook::find_free_layer(sb), 2);
}

TEST(LayerBookTest, PickVictimIsLeastRecentlyUsed) {
  LayerBook lb{kSym};
  auto& sb = lb.side_book(kSym, common::Side::kBuy);

  for (int i = 0; i < kSlotsPerSide; ++i) {
    sb.slots[i].last_used = static_cast<uint64_t>(100 + i);
  }
  EXPECT_EQ(LayerBook::pick_victim_layer(sb), 0);

  sb.slots[3].last_used = 50;
  EXPECT_EQ(LayerBook::pick_victim_layer(sb), 3);
}

TEST(LayerBookTest, GetOrAssignLayerUsesFreeThenVictimWithLiveFlag) {
  LayerBook lb{kSym};
  auto& sb = lb.side_book(kSym, common::Side::kBuy);

  uint64_t now = 1;
  for (int i = 0; i < kSlotsPerSide; ++i) {
    auto a = LayerBook::get_or_assign_layer(sb, tick(100 + i), now++);
    sb.slots[a.layer].state = OMOrderState::kLive;
  }
  EXPECT_EQ(LayerBook::pick_victim_layer(sb), 0);

  auto a2 = LayerBook::get_or_assign_layer(sb, tick(9999), /*now=*/1000);
  EXPECT_EQ(a2.layer, 0);
  ASSERT_TRUE(a2.victim_live_layer.has_value());
  EXPECT_EQ(*a2.victim_live_layer, 0);
  EXPECT_EQ(sb.layer_ticks[0], tick(9999));
  EXPECT_EQ(sb.slots[0].last_used, 1000u);
}

TEST(LayerBookTest, UnmapLayerClearsTickOnly) {
  LayerBook lb{kSym};
  auto& sb = lb.side_book(kSym, common::Side::kSell);

  auto a = LayerBook::get_or_assign_layer(sb, tick(777), /*now=*/10);
  sb.slots[a.layer].state = OMOrderState::kLive;

  ASSERT_EQ(sb.layer_ticks[a.layer], tick(777));
  LayerBook::unmap_layer(sb, a.layer);
  EXPECT_EQ(sb.layer_ticks[a.layer], kTicksInvalid);
  EXPECT_EQ(sb.slots[a.layer].state, OMOrderState::kLive);
}

TEST(LayerBookTest, BuySellBooksAreIndependent) {
  LayerBook lb{kSym};
  auto& buy = lb.side_book(kSym, common::Side::kBuy);
  auto& sell = lb.side_book(kSym, common::Side::kSell);

  auto ab = LayerBook::get_or_assign_layer(buy, tick(123), /*now=*/1);
  auto as = LayerBook::get_or_assign_layer(sell, tick(456), /*now=*/2);

  EXPECT_EQ(LayerBook::find_layer_by_ticks(buy, tick(123)), ab.layer);
  EXPECT_EQ(LayerBook::find_layer_by_ticks(sell, tick(456)), as.layer);

  // 서로 간섭 없음
  EXPECT_EQ(LayerBook::find_layer_by_ticks(buy, tick(456)), -1);
  EXPECT_EQ(LayerBook::find_layer_by_ticks(sell, tick(123)), -1);
}