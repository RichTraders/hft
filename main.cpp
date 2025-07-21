/*
* MIT License
 *
 * Copyright (c) 2025 NewOro Corporation
 *
 * Permission is hereby granted, free of charge, to use, copy, modify, and
 * distribute this software for any purpose with or without fee, provided that
 * the above copyright notice appears in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 */

#include "hft_lib.h"
#include "market_consumer.h"
#include "thread.hpp"

int main() {
  common::Thread<common::PriorityTag<1>, common::AffinityTag<1>>
      consumer_thread;
  consumer_thread.start([]() {
    const MarketConsumer consumer;
    while (true) {}
  });

  consumer_thread.join();
  return 0;
}