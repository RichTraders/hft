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
#include <sched.h>
#include <fstream>
#include "../hft/common/CpuManager/cpu_manager.h"
#include "ini_config.hpp"
#include "logger.h"
#include "thread.hpp"

using namespace common;

namespace {
void run() {
  while (1) {
    sleep(1);
  }
}

bool is_in_iso_slice() {
  std::ifstream cgroup_file("/proc/self/cgroup");
  if (!cgroup_file)
    return false;

  std::string line;
  while (std::getline(cgroup_file, line)) {
    if (line.find("iso.slice") != std::string::npos)
      return true;
  }
  return false;
}
}  // namespace

// Integration test to verify full CPU management in systemd iso.slice
// This test validates:
// 1. Cgroup detection and validation (via init_cpu_group)
// 2. Thread affinity assignment
// 3. Scheduler policy configuration (SCHED_RR, SCHED_FIFO, SCHED_BATCH)
TEST(CpuCgroupIntegrationTest, FullCpuManagementInIsoSlice) {
  if (!is_in_iso_slice()) {
    GTEST_SKIP() << "Not running in iso.slice. Run with: "
                 << "sudo systemd-run --scope --slice=iso.slice "
                 << "-p AllowedCPUs=0-4 "
                 << "./cpu_cgroup_integration_tests";
  }

  INI_CONFIG.load("resources/cpu_integration_test_config.ini");

  Thread<"test_0"> test_thread_0;
  Thread<"test_1"> test_thread_1;
  Thread<"test_2"> test_thread_2;
  Thread<"test_3"> test_thread_3;
  Thread<"test_4"> test_thread_4;

  test_thread_0.start(&run);
  test_thread_1.start(&run);
  test_thread_2.start(&run);
  test_thread_3.start(&run);
  test_thread_4.start(&run);

  auto logger = std::make_unique<Logger>();
  CpuManager cpu(logger.get());

  // Verify cgroup validation succeeds in iso.slice
  std::string cgroup_result;
  EXPECT_EQ(cpu.init_cpu_group(cgroup_result), 0)
      << "Cgroup validation should succeed in iso.slice. Error: " << cgroup_result;

  // Test affinity and scheduler setup
  EXPECT_FALSE(cpu.init_cpu_to_tid());

  // Verify scheduler policies
  int pol_0 = sched_getscheduler(cpu.get_tid("test_0"));
  int pol_1 = sched_getscheduler(cpu.get_tid("test_1"));
  int pol_2 = sched_getscheduler(cpu.get_tid("test_2"));
  int pol_3 = sched_getscheduler(cpu.get_tid("test_3"));
  int pol_4 = sched_getscheduler(cpu.get_tid("test_4"));

  EXPECT_EQ(pol_0, 2) << "test_0 should use SCHED_RR";
  EXPECT_EQ(pol_1, 2) << "test_1 should use SCHED_RR";
  EXPECT_EQ(pol_2, 1) << "test_2 should use SCHED_FIFO";
  EXPECT_EQ(pol_3, 1) << "test_3 should use SCHED_FIFO";
  EXPECT_EQ(pol_4, 3) << "test_4 should use SCHED_BATCH";

  // Verify CPU affinity
  cpu_set_t cpu_set;
  CPU_ZERO(&cpu_set);
  ASSERT_EQ(sched_getaffinity(cpu.get_tid("test_0"), sizeof(cpu_set), &cpu_set), 0);
  EXPECT_TRUE(CPU_ISSET(0, &cpu_set)) << "test_0 should be pinned to CPU 0";

  CPU_ZERO(&cpu_set);
  ASSERT_EQ(sched_getaffinity(cpu.get_tid("test_4"), sizeof(cpu_set), &cpu_set), 0);
  EXPECT_TRUE(CPU_ISSET(4, &cpu_set)) << "test_4 should be pinned to CPU 4";
}
