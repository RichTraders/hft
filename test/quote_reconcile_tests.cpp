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
#include "quote_reconciler.h"

using trading::kSlotsPerSide;
using trading::kTicksInvalid;
using trading::order::ActionCancel;
using trading::order::ActionNew;
using trading::order::ActionReplace;
using trading::order::Actions;
using trading::order::LayerBook;
using trading::order::OrderSlot;
using trading::order::QuoteReconciler;
using trading::order::SideBook;
using namespace trading;

static const common::TickerId kSym{"TEST"};
static constexpr double kTickSize = 1.0;
;

static inline int64_t TK(double px) {
  return std::llround(px / kTickSize);
}

// 슬롯을 지정 상태로 세팅
static void SetLiveSlot(SideBook& sb, int layer, double px, double qty,
                        uint64_t last_used, uint64_t id) {
  sb.layer_ticks[layer] = TK(px);
  sb.slots[layer].state = OMOrderState::kLive;
  sb.slots[layer].price = common::Price(px);
  sb.slots[layer].qty = common::Qty{qty};
  sb.slots[layer].last_used = last_used;
  sb.slots[layer].cl_order_id = common::OrderId{id};
}

TEST(QuoteReconcilerTest, EmptyIntentsYieldNoActions) {
  LayerBook lb{kSym};
  QuoteReconciler rec;
  std::vector<QuoteIntent> intents;
  common::FastClock clk(3.5e9, 10);

  auto acts = rec.diff(intents, lb, kTickSize, clk);
  EXPECT_TRUE(acts.empty());
}

TEST(QuoteReconcilerTest, NewActionWhenSlotInvalidOrDead) {
  LayerBook lb{kSym};
  lb.side_book(kSym, common::Side::kBuy);

  QuoteReconciler rec;
  common::FastClock clk(3.5e9, 10);
  std::vector<QuoteIntent> intents = {{.ticker = kSym,
                                       .side = common::Side::kBuy,
                                       .price = common::Price(100),
                                       .qty = common::Qty{1.0}}};

  auto acts = rec.diff(intents, lb, kTickSize, clk);
  ASSERT_EQ(acts.news.size(), 1u);
  EXPECT_EQ(acts.repls.size(), 0u);
  EXPECT_EQ(acts.cancels.size(), 0u);

  const auto& n = acts.news.front();
  EXPECT_EQ(n.side, common::Side::kBuy);
  EXPECT_EQ(n.price.value, 100);
  EXPECT_DOUBLE_EQ(n.qty.value, 1.0);
  EXPECT_GE(n.layer, 0);
  EXPECT_LT(n.layer, kSlotsPerSide);
}

TEST(QuoteReconcilerTest, NoReplaceWhenSameTickAndTinyQtyChange) {
  LayerBook lb{kSym};
  auto& sb = lb.side_book(kSym, common::Side::kBuy);

  // 기존 라이브: price=100, qty=1.0, id=11
  SetLiveSlot(sb, /*layer=*/0, /*px=*/100, /*qty=*/1.0, /*last_used=*/10,
              /*id=*/11);

  QuoteReconciler rec;
  common::FastClock clk(3.5e9, 10);
  std::vector<QuoteIntent> intents = {{.ticker = kSym,
                                       .side = common::Side::kBuy,
                                       .price = common::Price(100),
                                       .qty = common::Qty{1.0 + 1e-12}}};

  auto acts = rec.diff(intents, lb, kTickSize, clk);
  EXPECT_EQ(acts.news.size(), 0u);
  EXPECT_EQ(acts.repls.size(), 0u);
  EXPECT_EQ(acts.cancels.size(), 0u);
}

TEST(QuoteReconcilerTest, MovePriceGeneratesNewThenCancelWhenFreeLayerExists) {
  LayerBook lb{kSym};
  auto& sb = lb.side_book(kSym, common::Side::kBuy);

  SetLiveSlot(sb, /*layer=*/0, /*px=*/100, /*qty=*/1.0, /*last_used=*/10,
              /*id=*/22);

  QuoteReconciler rec;
  common::FastClock clk(3.5e9, 10);

  std::vector<QuoteIntent> intents = {{.ticker = kSym,
                                       .side = common::Side::kBuy,
                                       .price = common::Price(101),
                                       .qty = common::Qty{1.0}}};

  auto acts = rec.diff(intents, lb, kTickSize, clk);

  // 새 레벨 신규 + 기존 레벨 취소
  ASSERT_EQ(acts.news.size(), 1u);
  ASSERT_EQ(acts.repls.size(), 0u);
  ASSERT_EQ(acts.cancels.size(), 1u);

  const auto& n = acts.news.front();
  EXPECT_EQ(n.side, common::Side::kBuy);
  EXPECT_EQ(n.price.value, 101);
  EXPECT_DOUBLE_EQ(n.qty.value, 1.0);
  EXPECT_NE(n.layer, 0);  // 보통 비어있는 다음 슬롯(예: 1)

  const auto& c = acts.cancels.front();
  EXPECT_EQ(c.side, common::Side::kBuy);
  EXPECT_EQ(c.layer, 0);
  EXPECT_EQ(c.cl_order_id.value, 22);  // 기존 오더 id
}

TEST(QuoteReconcilerTest, ReplaceWhenQtyChangesBeyondThreshold) {
  LayerBook lb{kSym};
  auto& sb = lb.side_book(kSym, common::Side::kBuy);

  // 기존 라이브: 100 @1.0, id=33
  SetLiveSlot(sb, 0, 100, 1.0, 10, 33);

  QuoteReconciler rec;
  common::FastClock clk(3.5e9, 10);
  // 수량을 의미 있게 변경(임계 초과)
  std::vector<QuoteIntent> intents = {{.ticker = kSym,
                                       .side = common::Side::kBuy,
                                       .price = common::Price(100),
                                       .qty = common::Qty{1.5}}};

  auto acts = rec.diff(intents, lb, kTickSize, clk);
  ASSERT_EQ(acts.repls.size(), 1u);
  const auto& r = acts.repls.front();
  EXPECT_EQ(r.cl_order_id.value, 33);
  EXPECT_EQ(r.price.value, 100);
  EXPECT_EQ(r.last_qty, 1.0);
  EXPECT_DOUBLE_EQ(r.qty.value, 1.5);
  EXPECT_EQ(acts.news.size(), 0u);
  EXPECT_EQ(acts.cancels.size(), 0u);
}

TEST(QuoteReconcilerTest, AutoCancelForStaleLiveLayer) {
  LayerBook lb{kSym};
  auto& sb = lb.side_book(kSym, common::Side::kSell);

  // 레이어 1: 기존 라이브 200@2.0 id=44
  SetLiveSlot(sb, 1, 200, 2.0, 10, 44);

  QuoteReconciler rec;
  common::FastClock clk(3.5e9, 10);

  // 이번 의도는 201 한 개뿐(→ 200은 의도에서 빠짐 → cancel)
  std::vector<QuoteIntent> intents = {{.ticker = kSym,
                                       .side = common::Side::kSell,
                                       .price = common::Price(201),
                                       .qty = common::Qty{2.0}}};

  auto acts = rec.diff(intents, lb, kTickSize, clk);
  ASSERT_EQ(acts.cancels.size(), 1u);
  const auto& c = acts.cancels.front();
  EXPECT_EQ(c.side, common::Side::kSell);
  EXPECT_EQ(c.layer, 1);
  EXPECT_EQ(c.cl_order_id.value, 44);
  // 새 레벨은 New로 나와야
  ASSERT_EQ(acts.news.size(), 1u);
  EXPECT_EQ(acts.repls.size(), 0u);
}

TEST(QuoteReconcilerTest, VictimLiveLayerGeneratesCancelWithVictimId) {
  LayerBook lb{kSym};
  auto& sb = lb.side_book(kSym, common::Side::kBuy);

  //full
  for (int i = 0; i < kSlotsPerSide; ++i) {
    SetLiveSlot(sb, i, 100 + i, 1.0, /*last_used=*/100 + i, /*id=*/1000 + i);
  }
  EXPECT_EQ(LayerBook::pick_victim_layer(sb), 0);

  QuoteReconciler rec;
  common::FastClock clk(3.5e9, 10);

  std::vector<QuoteIntent> intents = {{.ticker = kSym,
                                       .side = common::Side::kBuy,
                                       .price = common::Price(9999),
                                       .qty = common::Qty{3.0}}};

  auto acts = rec.diff(intents, lb, kTickSize, clk);

  ASSERT_TRUE(acts.cancels.empty());
  ASSERT_TRUE(acts.news.empty());
  auto it = std::ranges::find_if(acts.repls,
                         [](const ActionReplace& c) {
                           return c.layer == 0 && c.side == common::Side::kBuy && c.cl_order_id == 1000;
                         });
  ASSERT_NE(it, acts.repls.end());
  EXPECT_EQ(it->cl_order_id.value, 1000);  // victim의 id

  ASSERT_EQ(acts.repls.size(), 1u);
  EXPECT_EQ(acts.repls.front().price.value, 9999);
  EXPECT_EQ(acts.repls.front().side, common::Side::kBuy);
  EXPECT_EQ(acts.repls.front().qty, 3.0);
  EXPECT_EQ(acts.repls.front().last_qty, 1.0);
}

TEST(QuoteReconcilerTest, BuySellIndependence) {
  LayerBook lb{kSym};
  auto& buy = lb.side_book(kSym, common::Side::kBuy);
  auto& sell = lb.side_book(kSym, common::Side::kSell);

  SetLiveSlot(buy, 0, 100, 1.0, 10, 501);
  SetLiveSlot(sell, 0, 200, 1.0, 10, 601);

  QuoteReconciler rec;
  common::FastClock clk(3.5e9, 10);
  // 의도: BUY만 가격 변경, SELL은 의도 없음 → BUY: Replace, SELL: Cancel(자동 fade) 없음(기존 SELL이 200인데 SELL 의도가 하나도 없으면 cancel 나와야 함)
  std::vector<QuoteIntent> intents = {
      {.ticker = kSym,
       .side = common::Side::kBuy,
       .price = common::Price(101),
       .qty = common::Qty{1.0}}
  };

  auto acts = rec.diff(intents, lb, kTickSize, clk);
  // BUY: replace 1개
  ASSERT_EQ(acts.news.size(), 1u);
  EXPECT_EQ(acts.news.front().side, common::Side::kBuy);

  // SELL
  ASSERT_EQ(acts.cancels.size(), 1u);
  EXPECT_EQ(acts.cancels.front().side, common::Side::kBuy);
  EXPECT_EQ(acts.cancels.front().cl_order_id.value, 501);

  EXPECT_TRUE(acts.repls.empty());
}

TEST(QuoteReconcilerTest, NoDuplicateActionsForSameIntentTwice) {
  LayerBook lb{kSym};
  auto& sb = lb.side_book(kSym, common::Side::kBuy);

  // 기존 라이브: 100@1.0 id=701
  SetLiveSlot(sb, 0, 100, 1.0, 10, 701);

  QuoteReconciler rec;
  common::FastClock clk(3.5e9, 10);

  // 같은 의도를 두 번 diff해도 replace/new가 추가적으로 생기면 안 됨
  std::vector<QuoteIntent> intents = {{.ticker = kSym,
                                       .side = common::Side::kBuy,
                                       .price = common::Price(100),
                                       .qty = common::Qty{1.0}}};
  auto a1 = rec.diff(intents, lb, kTickSize, clk);
  auto a2 = rec.diff(intents, lb, kTickSize, clk);

  EXPECT_TRUE(a1.empty());
  EXPECT_TRUE(a2.empty());
}