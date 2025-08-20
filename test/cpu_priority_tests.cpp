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
#include "cpu_manager.h"
#include "logger.h"
#include "thread.hpp"
#include <pthread.h>

#include <pthread.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/sched.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <sched.h>
#include <time.h>
#include <atomic>
#include <iostream>
#include <iomanip>
#include <bits/stdc++.h>
#include <sys/syscall.h>
#include <sched.h>
#include <sys/capability.h>

#include "cpu_manager.h"
#include <sys/capability.h>
#include <sys/prctl.h>
#include <stdio.h>
#include <unistd.h>

using namespace common;

#ifndef SYS_sched_setattr
# define SYS_sched_setattr 314
#endif
#ifndef SYS_gettid
# define SYS_gettid 186
#endif

void run() {
  while(1) {
    sleep(1);
  }
}

TEST(CpuPriorityTest, CpuSetting) {
  common::Thread<"test_0"> test_thread_0;
  common::Thread<"test_1"> test_thread_1;
  common::Thread<"test_2"> test_thread_2;
  common::Thread<"test_3"> test_thread_3;


  test_thread_0.start(&run);
  test_thread_1.start(&run);
  test_thread_2.start(&run);
  test_thread_3.start(&run);

  auto logger = std::make_unique<Logger>();
  CpuManager cpu(logger.get());

  std::string result;
  EXPECT_FALSE(cpu.init_cpu_group(result));
  EXPECT_FALSE(cpu.init_cpu_to_tid());

  int pol_0 = sched_getscheduler(cpu.get_tid("test_0"));
  int pol_1 = sched_getscheduler(cpu.get_tid("test_1"));
  int pol_2 = sched_getscheduler(cpu.get_tid("test_2"));
  int pol_3 = sched_getscheduler(cpu.get_tid("test_3"));

  EXPECT_EQ(pol_0, 2);
  EXPECT_EQ(pol_1, 2);
  EXPECT_EQ(pol_2, 1);
  EXPECT_EQ(pol_3, 1);
}

TEST(CpuPriorityTest, CpuGroupAndTidTest) {
  auto logger = std::make_unique<Logger>();
  CpuManager cpu(logger.get());

  std::string result;
  EXPECT_FALSE(cpu.init_cpu_group(result));
  EXPECT_FALSE(cpu.init_cpu_to_tid());
}

TEST(CpuPriorityTest, DISABLED_CpuSettingWithoutIRQL) {
  common::Thread<"test_0"> test_thread_0;
  common::Thread<"test_1"> test_thread_1;
  common::Thread<"test_2"> test_thread_2;
  common::Thread<"test_3"> test_thread_3;


  test_thread_0.start(&run);
  test_thread_1.start(&run);
  test_thread_2.start(&run);
  test_thread_3.start(&run);

  auto logger = std::make_unique<Logger>();
  CpuManager cpu(logger.get());

  EXPECT_TRUE(cpu.init_cpu_to_tid());

  int pol_0 = sched_getscheduler(cpu.get_tid("test_0"));
  int pol_1 = sched_getscheduler(cpu.get_tid("test_1"));
  int pol_2 = sched_getscheduler(cpu.get_tid("test_2"));
  int pol_3 = sched_getscheduler(cpu.get_tid("test_3"));

  EXPECT_EQ(pol_0, 2);
  EXPECT_EQ(pol_1, 2);
  EXPECT_EQ(pol_2, 1);
  EXPECT_EQ(pol_3, 1);
}
