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

#include <charconv>

#ifdef __linux__
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>
#elif __APPLE__
#include <mach/thread_act.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#include <sys/qos.h>  // Apple QoS
#include <sys/sysctl.h>
#include <sys/types.h>
#endif

namespace common {
namespace {}
// NOLINTBEGIN(unused-parameter)
CpuManager::CpuManager(const Logger::Producer& logger) : logger_(logger) {
  const int cpu_use_count = INI_CONFIG.get_int("cpu_id", "count");
  use_cpu_group_ =
      static_cast<bool>(INI_CONFIG.get_int("cpu_id", "use_cpu_group"));
  use_cpu_to_tid_ =
      static_cast<bool>(INI_CONFIG.get_int("cpu_id", "use_cpu_to_tid"));

  for (int i = 0; i < cpu_use_count; i++) {
    const std::string cpu_id = "cpu_" + std::to_string(i);

    const CpuInfo info = {
        .use_irq = static_cast<bool>(INI_CONFIG.get_int(cpu_id, "use_irq")),
        .type = static_cast<uint8_t>(INI_CONFIG.get_int(cpu_id, "cpu_type"))};
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

    // 정책(SCHED_RR 등)에 따라 값(prio/nice) 로딩
#ifdef __linux__
    if (iter->second.type == SCHED_RR || iter->second.type == SCHED_FIFO) {
      info.value = INI_CONFIG.get_int(thread_id, "prio");
    } else {
      info.value = INI_CONFIG.get_int(thread_id, "nicev");
    }
#elif __APPLE__
    const char* config_key =
        (iter->second.type == 2 || iter->second.type == 1) ? "prio" : "nicev";

    info.value = INI_CONFIG.get_int(thread_id, config_key);
#endif

    info.tid = 0;
    thread_info_list_.emplace(thread_name, info);
  }
  logger_.info("[Constructor] Cpu manager Created");
}

CpuManager::~CpuManager() {
  logger_.info("[Destructor] Cpu manager Destroy");
}

void CpuManager::trim_newline(std::string& str) {
  if (!str.empty() && (str.back() == '\n' || str.back() == '\r'))
    str.pop_back();
}

// Thread Name -> TID 찾기
// Linux: /proc/pid/task 순회
// Apple: 외부 스레드 이름 조회 불가. (현재 스레드가 아니면 찾기 매우 어려움)
ThreadId CpuManager::get_tid_by_thread_name(const std::string& target_name) {
#ifdef __linux__
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
      auto res = std::from_chars(tid_str.data(),
          tid_str.data() + tid_str.size(),
          val,
          kDecimalBase);
      if (res.ec == std::errc{}) {
        return static_cast<ThreadId>(val);
      }
    }
  }
  return 0;

#elif __APPLE__
  logger_.warn("APPLE doesn't support get thread id. target id :{}",
      target_name);
  return 0;
#else
  return 0;
#endif
}

// NOLINTNEXTLINE(readability-make-member-function-const)
bool CpuManager::init_cpu_to_tid() {
  if (!use_cpu_to_tid_)  // NOLINTNEXTLINE(readability-simplify-boolean-expr)
    return true;

  for (auto& info : thread_info_list_) {
    const std::string& thread_name = info.first;
    const int value = info.second.value;
    const uint8_t cpu_id = info.second.cpu_id;

    const int tid = get_tid_by_thread_name(thread_name);
    if (tid == 0) {
#ifdef __APPLE__
      logger_.error(
          std::format("[CpuManager] macOS does not support finding threads by "
                      "name externally: '{}'",
              thread_name));
#else
      logger_.error(
          std::format("[CpuManager] Thread '{}' not found", thread_name));
#endif
      continue;
    }

    info.second.tid = tid;
    logger_.info(std::format("[CpuManager] Found thread '{}' with TID {}",
        thread_name,
        tid));

    const auto& cpu_info = cpu_info_list_.find(cpu_id);
    if (cpu_info == cpu_info_list_.end())
      return true;

    int ret = 0;
#ifdef __linux__
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
      default:
        ret = -1;
        break;
    }
#elif __APPLE__
    if (cpu_info->second.type == 1 /*FIFO*/)
      ret = set_cpu_fifo(cpu_id, tid, value);
    else if (cpu_info->second.type == 2 /*RR*/)
      ret = set_cpu_rr(cpu_id, tid, value);
    else
      ret = set_cpu_other(cpu_id, tid, value);
#endif
    if (ret != 0)
      return true;
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
  if (!use_cpu_group_)
    return 1;

#ifdef __linux__
  try {
    std::ifstream cgroup_file("/proc/self/cgroup");
    std::string cgroup_line;
    std::getline(cgroup_file, cgroup_line);

    if (cgroup_line.find("iso.slice") == std::string::npos) {
      result = "Process not in iso.slice. Run with systemd-run ...";
      return 1;
    }
    return 0;
  } catch (const std::exception& exception) {
    result = exception.what();
    return 1;
  }
#else
  // Apple/Others: cgroup 미지원
  result = "Not supported on this OS";
  return 0;  // 에러로 보지 않고 통과
#endif
}

int CpuManager::set_cpu_to_tid(uint8_t cpu_id, ThreadId tid) {
#ifdef __linux__
  cpu_set_t cpu_set;
  CPU_ZERO(&cpu_set);
  CPU_SET(cpu_id, &cpu_set);
  if (sched_setaffinity(tid, sizeof(cpu_set), &cpu_set) != 0) {
    logger_.error(
        std::format("[CpuManager] failed to set cpu({}) to tid({}): {}",
            cpu_id,
            tid,
            strerror(errno)));
    return -1;
  }
  // 확인 로직
  CPU_ZERO(&cpu_set);
  if (sched_getaffinity(tid, sizeof(cpu_set), &cpu_set) == -1) {
    return -1;
  }
  logger_.info(
      std::format("[CpuManager] tid {} allowed CPU : {}", tid, cpu_id));
  return 0;

#elif __APPLE__
  (void)cpu_id;
  (void)tid;
  logger_.info(
      "[CpuManager] CPU pinning (Affinity) is NOT supported on Apple Silicon. "
      "Ignored.");
  return 0;
#endif
}

int CpuManager::set_rt(const uint8_t cpu_id, ThreadId tid, SchedPolicy policy,
    int priority) {
#ifdef __linux__
  const int pmin = sched_get_priority_min(static_cast<int>(policy));
  const int pmax = sched_get_priority_max(static_cast<int>(policy));
  if (priority < pmin || priority > pmax)
    return -1;

  if (set_cpu_to_tid(cpu_id, tid))
    return -1;
  if (set_scheduler(tid, priority, static_cast<int>(policy)))
    return -1;
  return 0;

#elif __APPLE__
  (void)cpu_id;
  (void)policy;
  // Apple: RT 정책 대신 User Interactive QoS 적용 권장
  // 리눅스 코드 구조를 맞추기 위해 흉내만 냄
  logger_.info(
      std::format("[CpuManager] Setting High Priority (QoS) for TID {}", tid));

  // Apple에서는 pthread_t 핸들이 없으면 외부 스레드 QoS 설정이 불가함.
  // 여기서는 Nice 값(Priority)을 최대로 낮춰서(-20) 우선순위를 높임
  if (setpriority(PRIO_PROCESS, tid, priority) != 0) {
    logger_.error("[CpuManager] failed to set priority");
    return -1;
  }
  return 0;
#endif
}

int CpuManager::set_cfs(const uint8_t cpu_id, ThreadId tid, SchedPolicy policy,
    int nicev) {
  if (set_cpu_to_tid(cpu_id, tid) != 0) {
#ifdef __linux__
    return -1;
#endif
  }

#ifdef __linux__
  // 정책 변경 (OTHER, BATCH, IDLE)
  if (set_scheduler(tid, 0, static_cast<int>(policy))) {
    logger_.error("[CpuManager] failed to chrt to tid");
    return -1;
  }
#endif
  (void)policy;
  if (setpriority(PRIO_PROCESS, tid, nicev) != 0) {
    logger_.error(
        std::format("[CpuManager] failed to set nicev: {}", strerror(errno)));
    return -1;
  }
  return 0;
}

int CpuManager::set_cpu_fifo(const uint8_t cpu_id, ThreadId tid, int prio) {
#ifdef __linux__
  return set_rt(cpu_id, tid, SchedPolicy::kFinfo, prio);
#else
  // Apple: FIFO 개념 없음 -> High Priority로 처리
  return set_rt(cpu_id, tid, static_cast<SchedPolicy>(1), prio);
#endif
}

int CpuManager::set_cpu_rr(const uint8_t cpu_id, ThreadId tid, int prio) {
#ifdef __linux__
  return set_rt(cpu_id, tid, SchedPolicy::kRr, prio);
#else
  return set_rt(cpu_id, tid, static_cast<SchedPolicy>(2), prio);
#endif
}

int CpuManager::set_cpu_other(const uint8_t cpu_id, ThreadId tid, int nicev) {
#ifdef __linux__
  return set_cfs(cpu_id, tid, SchedPolicy::kOther, nicev);
#else
  return set_cfs(cpu_id, tid, static_cast<SchedPolicy>(0), nicev);
#endif
}

int CpuManager::set_cpu_batch(const uint8_t cpu_id, ThreadId tid, int nicev) {
#ifdef __linux__
  return set_cfs(cpu_id, tid, SchedPolicy::kBatch, nicev);
#else
  // Apple: Batch 없음 -> Nice 값만 적용
  return set_cfs(cpu_id, tid, static_cast<SchedPolicy>(0), nicev);
#endif
}

int CpuManager::set_cpu_idle(const uint8_t cpu_id, ThreadId tid, int nicev) {
#ifdef __linux__
  return set_cfs(cpu_id, tid, SchedPolicy::kIdle, nicev);
#else
  // Apple: QOS_CLASS_BACKGROUND가 적합하나, Nice 값으로 대체
  return set_cfs(cpu_id, tid, static_cast<SchedPolicy>(0), nicev);  // Max nice
#endif
}

int CpuManager::set_scheduler(ThreadId tid, int priority,
    int scheduler_policy) {
#ifdef __linux__
  const sched_param sched_params{.sched_priority = priority};
  if (sched_setscheduler(tid, scheduler_policy, &sched_params) != 0) {
    logger_.error(std::format("[CpuManager] failed to setscheduler: {}",
        strerror(errno)));
    return -1;
  }
  if (sched_getscheduler(tid) < 0) {
    return -1;
  }
  return 0;
#elif __APPLE__
  logger_.warn(
      "Apple doesn't support setscheduler. "
      "cpu_id:{},priority:{},scheduler_policy:{}",
      tid,
      priority,
      scheduler_policy);
  return 0;
#endif
}

#ifdef __linux__
int CpuManager::sched_setattr_syscall(ThreadId tid,
    const struct sched_attr* attr, unsigned int flags) {
  return static_cast<int>(syscall(SYS_sched_setattr, tid, attr, flags));
}
#endif

int CpuManager::set_affinity(const AffinityInfo& info) {
#ifdef __linux__
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
#else
  // Apple: Not supported
  logger_.warn("Apple doesn't support: setaffinity. {}", info.cpu_id_);
  return 0;
#endif
}

// NOLINTEND(unused-parameter)
}  // namespace common