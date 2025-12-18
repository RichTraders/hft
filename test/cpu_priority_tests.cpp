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
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "common/cpumanager/cpu_manager.h"
#include "ini_config.hpp"
#include "logger.h"
#include "thread.hpp"

using namespace common;

void run() {
  while(1) {
    sleep(1);
  }
}

TEST(CpuPriorityTest, CpuSetting) {
  INI_CONFIG.load("resources/cpu_test_config.ini");
  common::Thread<"test_0"> test_thread_0;
  common::Thread<"test_1"> test_thread_1;
  common::Thread<"test_2"> test_thread_2;
  common::Thread<"test_3"> test_thread_3;
  common::Thread<"test_4"> test_thread_4;


  test_thread_0.start(&run);
  test_thread_1.start(&run);
  test_thread_2.start(&run);
  test_thread_3.start(&run);
  test_thread_4.start(&run);

  auto logger = std::make_unique<Logger>();
  auto producer = logger->make_producer();
  CpuManager cpu(producer);

  EXPECT_FALSE(cpu.init_cpu_to_tid());

  int pol_0 = sched_getscheduler(cpu.get_tid("test_0"));
  int pol_1 = sched_getscheduler(cpu.get_tid("test_1"));
  int pol_2 = sched_getscheduler(cpu.get_tid("test_2"));
  int pol_3 = sched_getscheduler(cpu.get_tid("test_3"));
  int pol_4 = sched_getscheduler(cpu.get_tid("test_4"));

  EXPECT_EQ(pol_0, 2);
  EXPECT_EQ(pol_1, 2);
  EXPECT_EQ(pol_2, 1);
  EXPECT_EQ(pol_3, 1);
  EXPECT_EQ(pol_4, 3);
}

