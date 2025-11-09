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

#ifndef CPUMANAGER_CGROUP_CONTROLLER_H
#define CPUMANAGER_CGROUP_CONTROLLER_H

#include <filesystem>
#include <variant>

struct cgroup;
struct cgroup_controller;

inline constexpr std::string_view kCgroupRoot = "/sys/fs/cgroup";
inline constexpr std::string_view kCpusetController = "cpuset";

class CgroupException : public std::runtime_error {
 public:
  explicit CgroupException(const std::string& message)
      : std::runtime_error(message) {}
};

class CgroupInitException : public CgroupException {
 public:
  explicit CgroupInitException(const std::string& message)
      : CgroupException("Cgroup initialization failed: " + message) {}
};

class CgroupCreateException : public CgroupException {
 public:
  explicit CgroupCreateException(const std::string& message)
      : CgroupException("Cgroup creation failed: " + message) {}
};

class CgroupAttachException : public CgroupException {
 public:
  explicit CgroupAttachException(const std::string& message)
      : CgroupException("Cgroup attach failed: " + message) {}
};

class CgroupVerifyException : public CgroupException {
 public:
  explicit CgroupVerifyException(const std::string& message)
      : CgroupException("Cgroup verification failed: " + message) {}
};

class CgroupHandle {
 public:
  explicit CgroupHandle(const std::string& name);
  ~CgroupHandle();

  CgroupHandle(const CgroupHandle&) = delete;
  CgroupHandle& operator=(const CgroupHandle&) = delete;

  CgroupHandle(CgroupHandle&& other) noexcept;
  CgroupHandle& operator=(CgroupHandle&& other) noexcept;

  [[nodiscard]] cgroup* get() const { return cgroup_; }
  cgroup* release();

 private:
  cgroup* cgroup_;
};

class CgroupControllerHandle {
 public:
  CgroupControllerHandle(cgroup* grp, const std::string& controller_name);

  [[nodiscard]] cgroup_controller* get() const { return controller_; }

 private:
  cgroup_controller* controller_;
};

struct CgroupConfig {
  std::string cpu_range = "0-4";
  std::string name = "cpu_0_4";
  bool write_top_slices_direct = false;
};

class CgroupController {
 public:
  explicit CgroupController(CgroupConfig config);
  ~CgroupController() = default;

  void setup() const;
  void verify() const;
  void undo() const;
  void attach_pid(pid_t pid) const;
  static void detach_pid(pid_t pid);
  void overlap_scan() const;
  void part_fix() const;

  [[nodiscard]] const CgroupConfig& config() const { return config_; }

 private:
  void create_isolated_cgroup() const;
  void restrict_top_slices() const;
  void verify_isolated_cgroup() const;
  void verify_top_slices() const;
  static bool enable_cpuset_controller();

  static std::string read_file(const std::filesystem::path& path);
  static void write_file(const std::filesystem::path& path,
                         const std::string& data);
  static bool path_exists(const std::filesystem::path& path);

  static std::set<int> expand_set(const std::string& content);
  static std::string compress_set(const std::set<int>& set);
  static std::string subtract_set_str(const std::string& present,
                                      const std::string& remove);
  static bool sets_equal_str(const std::string& first,
                             const std::string& second);

  static std::string present_cpus_root();
  static std::string root_mems_effective();

  static void print_node_info(const std::filesystem::path& path);

  CgroupConfig config_;
};

struct Setup {};
struct Verify {};
struct Undo {};
struct PartFix {};
struct Overlap {};
struct Attach {
  int pid;
};
struct Detach {
  int pid;
};

using Command =
    std::variant<Setup, Verify, Undo, PartFix, Overlap, Attach, Detach>;

#endif  // CPUMANAGER_CGROUP_CONTROLLER_H
