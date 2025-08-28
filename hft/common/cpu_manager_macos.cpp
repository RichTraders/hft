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
  logger_->info("[Constructor] cpu manager start");
}

CpuManager::~CpuManager() {
  logger_->info("[Destructor] cpu manager start");
}

bool CpuManager::init_cpu_to_tid() {
  return true;
}

int CpuManager::get_tid(const std::string& ) {
  return -1;
}

int CpuManager::init_cpu_group(std::string& ) const {
  return -1;
}

void CpuManager::trim_newline(std::string& str) {
  if (!str.empty() && (str.back() == '\n' || str.back() == '\r'))
    str.pop_back();
}

// 이름이 유일하다는 가정. 못 찾으면 0 반환.
pid_t CpuManager::get_tid_by_thread_name(const std::string& ) {
  return -1;
}

int CpuManager::setup(std::string& ) {
  return -1;
}

int CpuManager::verify(std::string& ) {
  return -1;
}

int CpuManager::undo(std::string& ) {
  return -1;
}

int CpuManager::part_fix(std::string& ) {
  return -1;
}

int CpuManager::overlap(std::string& ) {
  return -1;
}

int CpuManager::attach(int , std::string& ) {
  return -1;
}

int CpuManager::detach(int , std::string& ) {
  return -1;
}

int CpuManager::sched_setattr_syscall(pid_t , const struct sched_attr* ,
                                      unsigned int ) {
  return -1;
}

int CpuManager::set_affinity(const AffinityInfo& ) {
  return -1;
}

int CpuManager::set_cpu_fifo(const uint8_t , pid_t , int ) {
  return -1;
}

int CpuManager::set_cpu_rr(const uint8_t , pid_t , int ) {
  return -1;
}

int CpuManager::set_cpu_other(const uint8_t , pid_t , int ) {
  return -1;
}

int CpuManager::set_cpu_batch(const uint8_t , pid_t , int ) {
  return -1;
}

int CpuManager::set_cpu_idle(const uint8_t , pid_t , int ) {
  return -1;
}

int CpuManager::set_rt(const uint8_t , pid_t , SchedPolicy ,
                       int ) {
  return -1;
}

int CpuManager::set_cfs(const uint8_t , pid_t , SchedPolicy ,
                        int ) {
  return -1;
}

int CpuManager::run_commnad(const std::string& , std::string& ) {
  return 1;
}

}  // namespace common