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

#pragma once

#include "../logger.h"

namespace common {
struct sched_attr;
struct CpuInfo {
  bool use_irq;
  uint8_t type;
};

struct ThreadInfo {
  uint8_t cpu_id;
  int value;
  int tid;
};

struct CpuId {
  uint8_t value;
};

struct ThreadId {
  pid_t value;
};

struct AffinityInfo {
  AffinityInfo(CpuId cpu, ThreadId tid) : cpu_id_(cpu.value), tid_(tid.value) {}
  uint8_t cpu_id_;
  pid_t tid_;
};

enum class SchedPolicy : uint8_t {
  kOther = 0,
  kFinfo = 1,
  kRr = 2,
  kBatch = 3,
  kIso = 4,
  kIdle = 5,
  kDeadline = 6
};

class Logger;
class CpuManager {
 public:
  explicit CpuManager(Logger* logger);
  ~CpuManager();

  int init_cpu_group(std::string& result) const;
  bool init_cpu_to_tid();
  int get_tid(const std::string& thread_name);

 private:
  static void trim_newline(std::string& str);
  static pid_t get_tid_by_thread_name(const std::string& target_name);
  static int sched_setattr_syscall(pid_t tid, const struct sched_attr* attr,
                                   unsigned int flags);
  int set_affinity(const AffinityInfo& info);
  int set_cpu_fifo(uint8_t cpu_id, pid_t tid, int prio);
  int set_cpu_rr(uint8_t cpu_id, pid_t tid, int prio);
  int set_cpu_other(uint8_t cpu_id, pid_t tid, int nicev);
  int set_cpu_batch(uint8_t cpu_id, pid_t tid, int nicev);
  int set_cpu_idle(uint8_t cpu_id, pid_t tid, int nicev);

  int set_rt(uint8_t cpu_id, pid_t tid, SchedPolicy policy, int priority);
  int set_cfs(uint8_t cpu_id, pid_t tid, SchedPolicy policy, int nicev);
  int set_cpu_to_tid(uint8_t cpu_id, pid_t tid);
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  int set_scheduler(pid_t tid, int priority, int scheduler_policy);

  Logger::Producer logger_;
  std::string set_cpu_file_path_;
  std::map<uint8_t, CpuInfo> cpu_info_list_;
  std::map<std::string, ThreadInfo> thread_info_list_;
  bool use_cpu_group_ = false;
  bool use_cpu_to_tid_ = false;
};
}  // namespace common
