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

#include "gmock/gmock.h"
#include "gtest/gtest.h"

TEST(SPSCQueueTest, SingleThreadBasic) {
  SPSCQueue<int> q(8);
  EXPECT_TRUE(q.empty());

  for (int i = 0; i < 8; ++i) {
    EXPECT_EQ(q.enqueue(i), i < 8);
  }
  EXPECT_TRUE(q.full());

  for (int i = 0; i < 8; ++i) {
    int v = -1;
    EXPECT_TRUE(q.dequeue(v));
    EXPECT_EQ(v, i);
  }
  EXPECT_TRUE(q.empty());
}

TEST(SPSCQueueTest, MultiThreadProducerConsumer) {
  constexpr std::size_t N = 100000;
  SPSCQueue<std::size_t> q(512);

  std::thread producer([&] {
    for (std::size_t i = 0; i < N; ++i) {
      while (!q.enqueue(i)) {}
    }
  });

  std::vector<std::size_t> results;
  results.reserve(N);
  std::thread consumer([&] {
    std::size_t v;
    while (results.size() < N) {
      if (q.dequeue(v))
        results.push_back(v);
    }
  });

  producer.join();
  consumer.join();

  for (std::size_t i = 0; i < N; ++i) {
    EXPECT_EQ(results[i], i);
  }
}

TEST(SPSCQueueTest, FullAndEmptyCycles) {
  SPSCQueue<int> q(4);

  for (int cycle = 0; cycle < 1000; ++cycle) {
    for (int i = 0; i < 4; ++i) {
      EXPECT_TRUE(q.enqueue(i));
    }
    EXPECT_TRUE(q.full());
    EXPECT_FALSE(q.enqueue(999));

    int v;
    for (int i = 0; i < 4; ++i) {
      EXPECT_TRUE(q.dequeue(v));
      EXPECT_EQ(v, i);
    }
    EXPECT_TRUE(q.empty());
    EXPECT_FALSE(q.dequeue(v));
  }
}