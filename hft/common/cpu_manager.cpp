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

#include "cpu_manager.h"
#include <sys/resource.h>
#include "ini_config.hpp"
#include "logger.h"

namespace common {
CpuManager::CpuManager(Logger* logger) {
  logger_ = logger;

  IniConfig cfg;
  cfg.load("resources/config.ini");

  const int cpu_use_count = cfg.get_int("cpu_id", "count");

  for (int i = 0; i < cpu_use_count; i++) {
    CpuInfo info;
    const std::string cpu_id = "cpu_" + std::to_string(i);

    info.use_irq = static_cast<bool>(cfg.get_int(cpu_id, "use_irq"));
    info.type = static_cast<uint8_t>(cfg.get_int(cpu_id, "cpu_type"));
    if (info.use_irq) {
      // irq 추가 필요, 현재 없으니 필요 없음
    }

    cpu_info_list_.emplace(i, info);
  }

  const int thread_count = cfg.get_int("thread", "count");

  for (int i = 0; i < thread_count; i++) {
    ThreadInfo info;
    const std::string thread_id = "thread_" + std::to_string(i);

    const std::string thread_name = cfg.get(thread_id, "name");

    info.cpu_id = static_cast<uint8_t>(cfg.get_int(thread_id, "cpu_id"));
    if (info.cpu_id <= SCHED_RR) {
      info.value = cfg.get_int(thread_id, "prio");
    } else {
      info.value = cfg.get_int(thread_id, "nicev");
    }

    info.tid = get_tid_by_thread_name(thread_name);
    thread_info_list_.emplace(thread_name, info);
  }
}

CpuManager::~CpuManager() {
  std::string result;
  detach(getpid(), result);
  undo(result);
}

bool CpuManager::init_cpu_to_tid() {
  for (const auto& info : thread_info_list_) {
    const int value = info.second.value;
    const uint8_t cpu_id = info.second.cpu_id;
    const int tid = info.second.tid;

    const auto& cpu_info = cpu_info_list_.find(cpu_id);

    if (cpu_info == cpu_info_list_.end()) {
      return true;
    }

    int ret = 0;
    switch (cpu_info->second.type) {
      case SCHED_FIFO:
        ret = set_cpu_fifo(cpu_id, tid, value);
        break;
      case SCHED_RR:
        ret = set_cpu_rr(cpu_id, tid, value);
        break;
      case SCHED_OTHER:
        ret = set_cpu_other(cpu_id, tid, value);
        break;
      case SCHED_BATCH:
        ret = set_cpu_batch(cpu_id, tid, value);
        break;
      case SCHED_IDLE:
        ret = set_cpu_idle(cpu_id, tid, value);
        break;
      case SCHED_DEADLINE:
      default:
        ret = -1;
        break;
    }

    if (ret != 0) {
      return true;
    }
  }

  return false;
}

int CpuManager::get_tid(const std::string& thread_name) {
  const auto& thread_info = thread_info_list_.find(thread_name);

  if (thread_info == thread_info_list_.end()) {
    return -1;
  }

  return thread_info->second.tid;
}

int CpuManager::init_cpu_group(std::string& result) {
  if (setup(result)) {
    return 1;
  }

  result.clear();

  if (part_fix(result)) {
    return 2;
  }

  result.clear();

  if (overlap(result)) {
    return 3;
  }

  result.clear();

  if (verify(result)) {
    return 4;
  }

  result.clear();

  if (attach(getpid(), result))
    return -1;

  return 0;
}

void CpuManager::trim_newline(std::string& str) {
  if (!str.empty() && (str.back() == '\n' || str.back() == '\r'))
    str.pop_back();
}

// 이름이 유일하다는 가정. 못 찾으면 0 반환.
pid_t CpuManager::get_tid_by_thread_name(const std::string& target_name) {
  const std::filesystem::path task_dir =
      std::filesystem::path("/proc") / std::to_string(getpid()) / "task";
  std::error_code err_c;

  if (!std::filesystem::exists(task_dir, err_c) ||
      !std::filesystem::is_directory(task_dir, err_c)) {
    return 0;
  }

  for (const auto& entry :
       std::filesystem::directory_iterator(task_dir, err_c)) {
    if (err_c)
      break;
    if (!entry.is_directory())
      continue;

    std::ifstream fin(entry.path() / "comm");
    if (!fin)
      continue;

    std::string name;
    std::getline(fin, name);
    trim_newline(name);

    if (name == target_name) {
      const std::string tid_str = entry.path().filename().string();
      int64_t val = 0;
      constexpr int kFormat = 10;
      auto res = std::from_chars(tid_str.data(),
                                 tid_str.data() + tid_str.size(), val, kFormat);
      if (res.ec == std::errc{}) {
        return static_cast<pid_t>(val);
      }
    }
  }
  return 0;  // not found
}

int CpuManager::setup(std::string& result) {
  return run_commnad("sudo ./set_cpu.sh setup", result);
}

int CpuManager::verify(std::string& result) {
  return run_commnad("sudo ./set_cpu.sh verify", result);
}

int CpuManager::undo(std::string& result) {
  return run_commnad("sudo ./set_cpu.sh undo", result);
}

int CpuManager::part_fix(std::string& result) {
  return run_commnad("sudo ./set_cpu.sh part-fix", result);
}

int CpuManager::overlap(std::string& result) {
  return run_commnad("sudo ./set_cpu.sh overlap", result);
}

int CpuManager::attach(int pid, std::string& result) {
  return run_commnad("sudo ./set_cpu.sh attach " + std::to_string(pid), result);
}

int CpuManager::detach(int pid, std::string& result) {
  return run_commnad("sudo ./set_cpu.sh detach " + std::to_string(pid), result);
}

int CpuManager::sched_setattr_syscall(pid_t tid, const struct sched_attr* attr,
                                      unsigned int flags) {
  return static_cast<int>(syscall(SYS_sched_setattr, tid, attr, flags));
}

int CpuManager::set_affinity(const AffinityInfo& info) {
  cpu_set_t cpu_info;

  CPU_ZERO(&cpu_info);
  CPU_SET(info.cpu_id_, &cpu_info);

  if (sched_setaffinity(info.tid_, sizeof(cpu_info), &cpu_info) != 0) {
    return -1;
  }
  return 0;
}

int CpuManager::set_cpu_fifo(const uint8_t cpu_id, pid_t tid, int prio) {
  return set_rt(cpu_id, tid, SchedPolicy::kFinfo, prio);
}

int CpuManager::set_cpu_rr(const uint8_t cpu_id, pid_t tid, int prio) {
  return set_rt(cpu_id, tid, SchedPolicy::kRr, prio);
}

int CpuManager::set_cpu_other(const uint8_t cpu_id, pid_t tid, int nicev) {
  return set_cfs(cpu_id, tid, SchedPolicy::kOther, nicev);
}

int CpuManager::set_cpu_batch(const uint8_t cpu_id, pid_t tid, int nicev) {
  return set_cfs(cpu_id, tid, SchedPolicy::kBatch, nicev);
}

int CpuManager::set_cpu_idle(const uint8_t cpu_id, pid_t tid, int nicev) {
  return set_cfs(cpu_id, tid, SchedPolicy::kIdle, nicev);
}

int CpuManager::set_rt(const uint8_t cpu_id, pid_t tid, SchedPolicy policy,
                       int prio) {
  const int pmin = sched_get_priority_min(static_cast<int>(policy));
  const int pmax = sched_get_priority_max(static_cast<int>(policy));
  if (prio < pmin || prio > pmax) {
    return -1;
  }

  if (set_affinity(AffinityInfo(CpuId(cpu_id), ThreadId(tid))) != 0) {
    return -1;
  }

  struct sched_param sched_params {};
  sched_params.sched_priority = prio;
  if (sched_setscheduler(tid, static_cast<int>(policy), &sched_params) != 0) {
    return -1;
  }
  return 0;
}

int CpuManager::set_cfs(const uint8_t cpu_id, pid_t tid, SchedPolicy policy,
                        int nicev) {
  if (set_affinity(AffinityInfo(CpuId(cpu_id), ThreadId(tid))) != 0) {
    return -1;
  }

  const struct sched_param sched_params {};
  if (sched_setscheduler(tid, static_cast<int>(policy), &sched_params) != 0) {
    return -1;
  }
  if (setpriority(PRIO_PROCESS, tid, nicev) != 0) {
    return -1;
  }
  return 0;
}

int CpuManager::run_commnad(const std::string& command, std::string& result) {
  constexpr int kCommandBufSize = 4096;
  std::array<char, kCommandBufSize> buf{};

  std::unique_ptr<FILE, int (*)(FILE*)> pipe(popen(command.c_str(), "r"),
                                             pclose);
  if (!pipe) {
    return 1;
  }

  while (fgets(buf.data(), buf.size(), pipe.get()))
    result += buf.data();

  return pclose(pipe.release());
}

}  // namespace common