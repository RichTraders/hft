//
// Created by neworo2 on 25. 8. 15.
//

#pragma once
#include <string>

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

  static int init_cpu_group(std::string& result);
  bool init_cpu_to_tid();
  int get_tid(const std::string& thread_name);

 private:
  static void trim_newline(std::string& str);
  static pid_t get_tid_by_thread_name(const std::string& target_name);
  static int setup(std::string& result);
  static int verify(std::string& result);
  static int undo(std::string& result);
  static int part_fix(std::string& result);
  static int overlap(std::string& result);
  static int attach(int pid, std::string& result);
  static int detach(int pid, std::string& result);
  static int sched_setattr_syscall(pid_t tid, const struct sched_attr* attr,
                                   unsigned int flags);
  static int set_affinity(const AffinityInfo& info);
  static int set_cpu_fifo(uint8_t cpu_id, pid_t tid, int prio);
  static int set_cpu_rr(uint8_t cpu_id, pid_t tid, int prio);
  static int set_cpu_other(uint8_t cpu_id, pid_t tid, int nicev);
  static int set_cpu_batch(uint8_t cpu_id, pid_t tid, int nicev);
  static int set_cpu_idle(uint8_t cpu_id, pid_t tid, int nicev);

  static int set_rt(uint8_t cpu_id, pid_t tid, SchedPolicy policy, int prio);
  static int set_cfs(uint8_t cpu_id, pid_t tid, SchedPolicy policy, int nicev);
  static int run_commnad(const std::string& command, std::string& result);

  Logger* logger_;
  std::string set_cpu_file_path_;
  uint8_t cpu_id_start_;
  uint8_t cpu_id_end_;
  std::map<uint8_t, CpuInfo> cpu_info_list_;
  std::map<std::string, ThreadInfo> thread_info_list_;
};
}  // namespace common
