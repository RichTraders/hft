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
#include "../ini_config.hpp"

namespace common {
CpuManager::CpuManager(Logger* logger) {
  logger_ = logger->make_producer();

  const int cpu_use_count = INI_CONFIG.get_int("cpu_id", "count");
  use_cpu_group_ =
      static_cast<bool>(INI_CONFIG.get_int("cpu_id", "use_cpu_group"));
  use_cpu_to_tid_ =
      static_cast<bool>(INI_CONFIG.get_int("cpu_id", "use_cpu_to_tid"));

  for (int i = 0; i < cpu_use_count; i++) {
    CpuInfo info;
    const std::string cpu_id = "cpu_" + std::to_string(i);

    info.use_irq = static_cast<bool>(INI_CONFIG.get_int(cpu_id, "use_irq"));
    info.type = static_cast<uint8_t>(INI_CONFIG.get_int(cpu_id, "cpu_type"));
    if (info.use_irq) {
      // irq 추가 필요, 현재 없으니 필요 없음
    }

    cpu_info_list_.emplace(i, info);
  }

  const int thread_count = INI_CONFIG.get_int("thread", "count");

  for (int i = 0; i < thread_count; i++) {
    ThreadInfo info;
    const std::string thread_id = "thread_" + std::to_string(i);

    const std::string thread_name = INI_CONFIG.get(thread_id, "name");

    info.cpu_id = static_cast<uint8_t>(INI_CONFIG.get_int(thread_id, "cpu_id"));

    const auto& iter = cpu_info_list_.find(info.cpu_id);

    if (iter == cpu_info_list_.end()) {
      logger_.error("[CpuManager] failed to get cpu_id info");
    }

    if (iter->second.type == SCHED_RR || iter->second.type == SCHED_FIFO) {
      info.value = INI_CONFIG.get_int(thread_id, "prio");
    } else {
      info.value = INI_CONFIG.get_int(thread_id, "nicev");
    }

    info.tid = 0;
    thread_info_list_.emplace(thread_name, info);
  }

  logger_.info("[Constructor] Cpu manager Created");
}

CpuManager::~CpuManager() {
  logger_.info("[Destructor] Cpu manager Destroy");
}

bool CpuManager::init_cpu_to_tid() {
  if (!use_cpu_to_tid_) {
    return true;
  }

  for (auto& info : thread_info_list_) {
    const std::string& thread_name = info.first;
    const int value = info.second.value;
    const uint8_t cpu_id = info.second.cpu_id;

    const int tid = get_tid_by_thread_name(thread_name);
    if (tid == 0) {
      logger_.error(
          std::format("[CpuManager] Thread '{}' not found", thread_name));
      return true;
    }

    info.second.tid = tid;
    logger_.info(std::format("[CpuManager] Found thread '{}' with TID {}",
                             thread_name, tid));

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

int CpuManager::init_cpu_group(std::string& result) const {
  if (!use_cpu_group_) {
    return 1;
  }

  try {
    std::ifstream cgroup_file("/proc/self/cgroup");
    std::string cgroup_line;
    std::getline(cgroup_file, cgroup_line);

    if (cgroup_line.find("iso.slice") == std::string::npos) {
      result =
          "Process not in iso.slice. Please run with: sudo systemd-run --scope "
          "--slice=iso.slice -p AllowedCPUs=0-4 ./HFT";
      return 1;
    }

    return 0;
  } catch (const std::exception& exception) {
    result = exception.what();
    return 1;
  }
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
  return 0;
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
    logger_.error(
        std::format("[CpuManager] sched_setaffinity :{}", strerror(errno)));
    return -1;
  }

  if (sched_getaffinity(info.tid_, sizeof(cpu_info), &cpu_info) == -1) {
    logger_.error(
        std::format("[CpuManager] sched_getaffinity :{}", strerror(errno)));
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
                       int priority) {
  const int pmin = sched_get_priority_min(static_cast<int>(policy));
  const int pmax = sched_get_priority_max(static_cast<int>(policy));
  if (priority < pmin || priority > pmax) {
    return -1;
  }

  if (set_cpu_to_tid(cpu_id, tid)) {
    logger_.error("[CpuManager] failed to cpu to tid");
    return -1;
  }

  if (set_scheduler(tid, priority, static_cast<int>(policy))) {
    logger_.error("[CpuManager] failed to chrt to tid");
    return -1;
  }

  return 0;
}

int CpuManager::set_cfs(const uint8_t cpu_id, pid_t tid, SchedPolicy policy,
                        int nicev) {
  if (set_affinity(AffinityInfo(CpuId(cpu_id), ThreadId(tid))) != 0) {
    return -1;
  }

  if (set_cpu_to_tid(cpu_id, tid)) {
    logger_.error("[CpuManager] failed to cpu to tid");
    return -1;
  }

  if (set_scheduler(tid, 0, static_cast<int>(policy))) {
    logger_.error("[CpuManager] failed to chrt to tid");
    return -1;
  }

  if (setpriority(PRIO_PROCESS, tid, nicev) != 0) {
    logger_.error(std::format(
        "[CpuManager] failed to priority to tid. Error:{}", strerror(errno)));
    return -1;
  }

  return 0;
}

int CpuManager::set_cpu_to_tid(uint8_t cpu_id, pid_t tid) {
  cpu_set_t cpu_set;
  CPU_ZERO(&cpu_set);
  CPU_SET(cpu_id, &cpu_set);
  if (sched_setaffinity(tid, sizeof(cpu_set), &cpu_set) != 0) {
    logger_.error(std::format(
        "[CpuManager] failed to set cpu({}) to tid({}): {} (errno={})", cpu_id,
        tid, strerror(errno), errno));
    return -1;
  }
  CPU_ZERO(&cpu_set);
  if (sched_getaffinity(tid, sizeof(cpu_set), &cpu_set) == -1) {
    logger_.error(
        std::format("[CpuManager] sched_getaffinity: {} cpu({}) to tid({})",
                    strerror(errno), cpu_id, tid));
    return -1;
  }
  logger_.info(
      std::format("[CpuManager] tid {} allowed CPU : {} ", tid, cpu_id));
  return 0;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
int CpuManager::set_scheduler(pid_t tid, int priority, int scheduler_policy) {
  const sched_param sched_params{.sched_priority = priority};
  if (sched_setscheduler(tid, scheduler_policy, &sched_params) != 0) {
    logger_.error(std::format(
        "[CpuManager] failed to set scheduler_policy({}) to tid({}). Error:{}",
        scheduler_policy, tid, strerror(errno)));
    return -1;
  }

  if (sched_getscheduler(tid) < 0) {
    logger_.error(std::format(
        "[CpuManager] failed to get scheduler_policy({}) to tid({})",
        scheduler_policy, tid));
    return -1;
  }
  return 0;
}

}  // namespace common