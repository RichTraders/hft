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
#include "fix_sequence_counter.h"
#include "gtest/gtest.h"
TEST(FixCounterTest, CheckFirstCome) {
  FixSequenceCounter counter;
  std::string test_message =
      "8=FIX.4.49=000011335=A49=SPOT56=BMDWATCH34=152=20250909-11:29:55."
      "53383198=0108=3025037=5a8455c3-bafd-45b3-8c76-fbc17d11853110=214";
  EXPECT_TRUE(counter.is_valid(test_message));
}

TEST(FixCounterTest, CheckNextData) {
  FixSequenceCounter counter;
  std::string test_message =
      "8=FIX.4.49=000011335=A49=SPOT56=BMDWATCH34=152=20250909-11:29:55."
      "53383198=0108=3025037=5a8455c3-bafd-45b3-8c76-fbc17d11853110=214";
  std::string test_message2 =
      "8=FIX.4.49=041011635=W49=SPOT56=BMDWATCH34=252=20250909-11:29:55."
      "915028262=DEPTH_STREAM55=BTCUSDT25044=76009613702268=10000269=0"
      "270=112697.61000000271=1.39946000269=0270=112697.60000000271=0."
      "00055000269=0270=112697.36000000271=0.00010000269=0270=112696."
      "59000000271=0.00005000269=0270=112696.58000000271=0.29299000269=0"
      "270=112696.";
  EXPECT_TRUE(counter.is_valid(test_message));
  EXPECT_TRUE(counter.is_valid(test_message2));
}

TEST(FixCounterTest, CheckFailData) {
  FixSequenceCounter counter;
  std::string test_message =
      "8=FIX.4.49=000011335=A49=SPOT56=BMDWATCH34=152=20250909-11:29:55."
      "53383198=0108=3025037=5a8455c3-bafd-45b3-8c76-fbc17d11853110=214";
  std::string test_message2 =
      "8=FIX.4.49=041011635=W49=SPOT56=BMDWATCH34=252=20250909-11:29:55."
      "915028262=DEPTH_STREAM55=BTCUSDT25044=76009613702268=10000269=0"
      "270=112697.61000000271=1.39946000269=0270=112697.60000000271=0."
      "00055000269=0270=112697.36000000271=0.00010000269=0270=112696."
      "59000000271=0.00005000269=0270=112696.58000000271=0.29299000269=0"
      "270=112696.";
  std::string test_message3 =
      "8=FIX.4.49=000045935=X49=SPOT56=BMDWATCH34=552=20250909-11:29:56."
      "114766262=DEPTH_STREAM268=8279=0269=0270=112682.03000000271=0."
      "4603400055=BTCUSDT25043=7600961373925044=76009613747279=0269=0270="
      "112447.45000000271=4.64418000279=2269=0270=112442.35000000279=2"
      "269=0270=112397.61000000279=1269=1270=112707.41000000271=0."
      "00448000279=2269=1270=112711.14000000279=0269=1270=112711."
      "26000000271=0.08874000279=0269=1270=112939.14000000271=4.67908000"
      "10=120";
  EXPECT_TRUE(counter.is_valid(test_message));
  EXPECT_TRUE(counter.is_valid(test_message2));
  EXPECT_FALSE(counter.is_valid(test_message3));
}
