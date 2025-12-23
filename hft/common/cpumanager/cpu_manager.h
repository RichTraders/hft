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

#ifndef CPU_MANAGER_H
#define CPU_MANAGER_H

#include "../logger.h"

#if __linux__
#include <sched.h>
#include <sys/types.h>
#endif

namespace common {

#ifdef __linux__
using ThreadId = pid_t;
#else
using ThreadId = int;
#endif

#if __linux__
struct sched_attr;
#endif

struct CpuInfo {
  bool use_irq;
  uint8_t type;
};

struct ThreadInfo {
  uint8_t cpu_id;
  int value;
  ThreadId tid;
};

struct CpuId {
  uint8_t value;
};

struct AffinityInfo {
  AffinityInfo(CpuId cpu, ThreadId tid) : cpu_id_(cpu.value), tid_(tid) {}
  uint8_t cpu_id_;
  ThreadId tid_;
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
  explicit CpuManager(const Logger::Producer& logger);
  ~CpuManager();

  int init_cpu_group(std::string& result) const;
  bool init_cpu_to_tid();
  int get_tid(const std::string& thread_name);

 private:
  static void trim_newline(std::string& str);

  ThreadId get_tid_by_thread_name(const std::string& target_name);
#if __linux__
  static int sched_setattr_syscall(ThreadId tid, const struct sched_attr* attr,
      unsigned int flags);
#endif
  int set_affinity(const AffinityInfo& info);
  int set_cpu_fifo(uint8_t cpu_id, ThreadId tid, int prio);
  int set_cpu_rr(uint8_t cpu_id, ThreadId tid, int prio);
  int set_cpu_other(uint8_t cpu_id, ThreadId tid, int nicev);
  int set_cpu_batch(uint8_t cpu_id, ThreadId tid, int nicev);
  int set_cpu_idle(uint8_t cpu_id, ThreadId tid, int nicev);

  int set_rt(uint8_t cpu_id, ThreadId tid, SchedPolicy policy, int priority);
  int set_cfs(uint8_t cpu_id, ThreadId tid, SchedPolicy policy, int nicev);
  int set_cpu_to_tid(uint8_t cpu_id, ThreadId tid);
  int set_scheduler(ThreadId tid, int priority, int scheduler_policy);

  const Logger::Producer& logger_;
  std::string set_cpu_file_path_;
  std::map<uint8_t, CpuInfo> cpu_info_list_;
  std::map<std::string, ThreadInfo> thread_info_list_;
  bool use_cpu_group_ = false;
  bool use_cpu_to_tid_ = false;
};
}  // namespace common

#endif