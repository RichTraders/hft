/*
 * MIT License
 *
 * Copyright (c) 2025 NewOro Corporation
 *
 * Permission is hereby granted, free of charge, to use, copy, modify, and
 * distribute this software for any purpose with or without fee, provided that
 * the above copyright notice appears in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 */

#include "cgroup_controller.h"

#include <libcgroup.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <system_error>

namespace fs = std::filesystem;

namespace {

inline constexpr std::array kSystemSlices = {"init.scope", "system.slice",
                                             "user.slice", "machine.slice"};

void initialize_libcgroup() {
  const int result = cgroup_init();
  if (result != 0) {
    throw CgroupInitException("cgroup_init() returned " +
                              std::to_string(result));
  }
}

}  // namespace

CgroupHandle::CgroupHandle(const std::string& name) : cgroup_(nullptr) {
  cgroup_ = cgroup_new_cgroup(name.c_str());
  if (!cgroup_) {
    throw CgroupCreateException("cgroup_new_cgroup failed for '" + name + "'");
  }
}

CgroupHandle::~CgroupHandle() {
  if (cgroup_) {
    cgroup_free(&cgroup_);
  }
}

CgroupHandle::CgroupHandle(CgroupHandle&& other) noexcept
    : cgroup_(other.cgroup_) {
  other.cgroup_ = nullptr;
}

CgroupHandle& CgroupHandle::operator=(CgroupHandle&& other) noexcept {
  if (this != &other) {
    if (cgroup_) {
      cgroup_free(&cgroup_);
    }
    cgroup_ = other.cgroup_;
    other.cgroup_ = nullptr;
  }
  return *this;
}

cgroup* CgroupHandle::release() {
  cgroup* temp = cgroup_;
  cgroup_ = nullptr;
  return temp;
}

CgroupControllerHandle::CgroupControllerHandle(
    cgroup* grp, const std::string& controller_name)
    : controller_(nullptr) {
  controller_ = cgroup_add_controller(grp, controller_name.c_str());
  if (!controller_) {
    throw CgroupCreateException("Failed to add controller '" + controller_name +
                                "'");
  }
}

CgroupController::CgroupController(CgroupConfig config)
    : config_(std::move(config)) {}

std::string CgroupController::read_file(const fs::path& path) {
  std::ifstream ifs(path);
  if (!ifs.is_open()) {
    throw std::runtime_error("Failed to open file: " + path.string());
  }

  std::ostringstream stream;
  stream << ifs.rdbuf();
  std::string content = stream.str();

  while (!content.empty() &&
         (content.back() == '\n' || content.back() == '\r')) {
    content.pop_back();
  }

  return content;
}

void CgroupController::write_file(const fs::path& path,
                                  const std::string& data) {
  std::ofstream ofs(path);
  if (!ofs.is_open()) {
    throw std::system_error(
        errno, std::generic_category(),
        "Failed to open file for writing: " + path.string());
  }

  ofs << data;
  if (!ofs.good()) {
    throw std::system_error(errno, std::generic_category(),
                            "Failed to write to file: " + path.string());
  }
}

bool CgroupController::path_exists(const fs::path& path) {
  return fs::exists(path);
}

std::set<int> CgroupController::expand_set(const std::string& content) {
  std::set<int> result;
  if (content.empty()) {
    return result;
  }

  std::istringstream iss(content);
  std::string token;

  while (std::getline(iss, token, ',')) {
    if (token.empty()) {
      continue;
    }

    const auto dash_pos = token.find('-');
    if (dash_pos == std::string::npos) {
      result.insert(std::stoi(token));
    } else {
      int first = std::stoi(token.substr(0, dash_pos));
      int last = std::stoi(token.substr(dash_pos + 1));
      if (first > last) {
        std::swap(first, last);
      }
      for (int i = first; i <= last; ++i) {
        result.insert(i);
      }
    }
  }

  return result;
}

std::string CgroupController::compress_set(const std::set<int>& set) {
  if (set.empty()) {
    return "";
  }

  const std::vector<int> vec(set.begin(), set.end());
  std::ostringstream oss;
  int range_start = vec[0];
  int range_end = vec[0];

  auto flush_range = [&](int start, int end) {
    if (!oss.str().empty()) {
      oss << ",";
    }
    if (start == end) {
      oss << start;
    } else {
      oss << start << "-" << end;
    }
  };

  for (size_t i = 1; i < vec.size(); ++i) {
    if (vec[i] == range_end + 1) {
      range_end = vec[i];
    } else {
      flush_range(range_start, range_end);
      range_start = range_end = vec[i];
    }
  }

  flush_range(range_start, range_end);
  return oss.str();
}

std::string CgroupController::subtract_set_str(const std::string& present,
                                               const std::string& remove) {
  std::set<int> present_set = expand_set(present);
  const std::set<int> remove_set = expand_set(remove);

  for (const auto cpu : remove_set) {
    present_set.erase(cpu);
  }

  return compress_set(present_set);
}

bool CgroupController::sets_equal_str(const std::string& first,
                                      const std::string& second) {
  return expand_set(first) == expand_set(second);
}

std::string CgroupController::present_cpus_root() {
  return read_file(fs::path(kCgroupRoot) / "cpuset.cpus.effective");
}

std::string CgroupController::root_mems_effective() {
  return read_file(fs::path(kCgroupRoot) / "cpuset.mems.effective");
}

bool CgroupController::enable_cpuset_controller() {
  const fs::path ctrl_path = fs::path(kCgroupRoot) / "cgroup.subtree_control";
  const std::string controllers =
      read_file(fs::path(kCgroupRoot) / "cgroup.controllers");

  if (controllers.find(kCpusetController) == std::string::npos) {
    std::cerr << "[ERROR] cpuset controller not available in this kernel\n";
    return false;
  }

  const std::string current = read_file(ctrl_path);
  if (current.find(kCpusetController) != std::string::npos) {
    return true;
  }

  try {
    write_file(ctrl_path, "+cpuset");
    return true;
  } catch (const std::exception& e) {
    std::cerr << "[ERROR] Failed to enable cpuset controller: " << e.what()
              << "\n";
    return false;
  }
}

void CgroupController::print_node_info(const fs::path& path) {
  try {
    const std::string eff = read_file(path / "cpuset.cpus.effective");
    const std::string part = read_file(path / "cpuset.cpus.partition");
    const std::string relative_path =
        path.string().substr(std::string(kCgroupRoot).size() + 1);

    std::cout << relative_path << "  eff=" << (eff.empty() ? "-" : eff)
              << "  part=" << (part.empty() ? "-" : part) << "\n";
  } catch (const std::exception& e) {
    std::cerr << "[ERROR] error :" << e.what() << "\n";
  }
}

void CgroupController::create_isolated_cgroup() const {
  initialize_libcgroup();

  if (!enable_cpuset_controller()) {
    std::cerr << "[WARNING] Could not ensure cpuset enabled; proceeding.\n";
  }

  const std::string mems = root_mems_effective();
  if (mems.empty()) {
    throw CgroupCreateException("Cannot read root cpuset.mems.effective");
  }

  const CgroupHandle cg_handle(config_.name);
  const CgroupControllerHandle controller(cg_handle.get(),
                                          std::string(kCpusetController));

  int result = cgroup_set_value_string(controller.get(), "cpuset.cpus",
                                       config_.cpu_range.c_str());
  if (result != 0) {
    throw CgroupCreateException("Failed to set cpuset.cpus: " +
                                std::to_string(result));
  }

  result =
      cgroup_set_value_string(controller.get(), "cpuset.mems", mems.c_str());
  if (result != 0) {
    throw CgroupCreateException("Failed to set cpuset.mems: " +
                                std::to_string(result));
  }

  result = cgroup_create_cgroup(cg_handle.get(), 0);
  if (result != 0) {
    throw CgroupCreateException("cgroup_create_cgroup failed: " +
                                std::to_string(result));
  }

  int result_part = cgroup_set_value_string(controller.get(),
                                            "cpuset.cpus.partition", "root");
  if (result_part == 0) {
    result = cgroup_modify_cgroup(cg_handle.get());
    if (result != 0) {
      result_part = -1;
    }
  }

  if (result_part != 0) {
    const fs::path part_file =
        fs::path(kCgroupRoot) / config_.name / "cpuset.cpus.partition";
    try {
      write_file(part_file, "root");
    } catch (const std::exception&) {
      std::cerr << "[WARNING] Partition root write failed; continuing\n";
    }
  }

  const fs::path cgroup_path = fs::path(kCgroupRoot) / config_.name;
  std::cout << "[OK] Created " << cgroup_path << "\n";
  std::cout << "     eff cpus: "
            << read_file(cgroup_path / "cpuset.cpus.effective") << "\n";
  std::cout << "     partition: "
            << read_file(cgroup_path / "cpuset.cpus.partition") << "\n";
}

void CgroupController::restrict_top_slices() const {
  const std::string present = present_cpus_root();
  if (present.empty()) {
    throw CgroupException("Cannot read root cpuset.cpus.effective");
  }

  const std::string allowed_cpus = subtract_set_str(present, config_.cpu_range);
  if (allowed_cpus.empty()) {
    throw CgroupException("No CPUs left for top-level slices");
  }

  const std::string mems = root_mems_effective();
  if (mems.empty()) {
    throw CgroupException("Cannot read root cpuset.mems.effective");
  }

  if (!config_.write_top_slices_direct) {
    std::cout << "[INFO] Use systemd to set AllowedCPUs on system slices.\n";
    std::cout << "       e.g., systemctl set-property --runtime system.slice "
                 "AllowedCPUs="
              << allowed_cpus << "\n";
    return;
  }

  for (const auto& slice : kSystemSlices) {
    const fs::path slice_path = fs::path(kCgroupRoot) / slice;
    if (!path_exists(slice_path)) {
      continue;
    }

    try {
      write_file(slice_path / "cpuset.cpus", allowed_cpus);
      write_file(slice_path / "cpuset.mems", mems);
    } catch (const std::exception& e) {
      throw CgroupException("Failed to write to slice " + std::string(slice) +
                            ": " + e.what());
    }
  }

  std::cout << "[OK] Restricted top slices to CPUs: " << allowed_cpus << "\n";
}

void CgroupController::setup() const {
  create_isolated_cgroup();
  restrict_top_slices();
}

void CgroupController::verify() const {
  verify_isolated_cgroup();
  verify_top_slices();
  std::cout << "[OK] VERIFY passed\n";
}

void CgroupController::undo() const {
  initialize_libcgroup();

  const CgroupHandle cg_handle(config_.name);
  if (const int result = cgroup_delete_cgroup(cg_handle.get(), 0);
      result != 0) {
    std::cerr << "[WARNING] Delete group failed (non-empty?): result=" << result
              << "\n";
  }

  std::cout << "[OK] Runtime revert (top slices may still have runtime/systemd "
               "settings)\n";
}

void CgroupController::attach_pid(pid_t pid) const {
  initialize_libcgroup();

  const CgroupHandle cg_handle(config_.name);

  try {
    const CgroupControllerHandle controller(cg_handle.get(),
                                            std::string(kCpusetController));
  } catch (const CgroupCreateException& e) {
    std::cerr << "[WARNING] Failed to attach to controller: " << e.what()
              << "\n";
  }

  if (const int result = cgroup_attach_task_pid(cg_handle.get(), pid);
      result != 0) {
    throw CgroupAttachException("Failed to attach PID " + std::to_string(pid) +
                                ": " + std::to_string(result));
  }

  std::cout << "[OK] Attached PID " << pid << " to "
            << (fs::path(kCgroupRoot) / config_.name) << "\n";
}

void CgroupController::detach_pid(pid_t pid) {
  const std::array<const char*, 2> ctrls = {kCpusetController.data(), nullptr};
  if (const int result = cgroup_change_cgroup_path("/", pid, ctrls.data());
      result != 0) {
    throw CgroupAttachException("Failed to detach PID " + std::to_string(pid) +
                                ": " + std::to_string(result));
  }

  std::cout << "[OK] Detached PID " << pid << " to /\n";
}

void CgroupController::overlap_scan() const {
  std::cout << "== TOP-LEVEL overlap ==\n";

  const std::array nodes = {fs::path(kCgroupRoot),
                            fs::path(kCgroupRoot) / "init.scope",
                            fs::path(kCgroupRoot) / "system.slice",
                            fs::path(kCgroupRoot) / "user.slice",
                            fs::path(kCgroupRoot) / "machine.slice",
                            fs::path(kCgroupRoot) / config_.name};

  for (const auto& node : nodes) {
    if (path_exists(node)) {
      print_node_info(node);
    }
  }

  for (const auto& entry : fs::directory_iterator(kCgroupRoot)) {
    const std::string name = entry.path().filename().string();
    if (name.starts_with("run-") && entry.is_directory()) {
      print_node_info(entry.path());
    }
  }
}

void CgroupController::part_fix() const {
  restrict_top_slices();

  const fs::path part_file =
      fs::path(kCgroupRoot) / config_.name / "cpuset.cpus.partition";
  if (path_exists(part_file)) {
    try {
      write_file(part_file, "root");
    } catch (const std::exception& e) {
      std::cerr << "[WARNING] Failed to write root file: " << e.what() << "\n";
    }
  }

  overlap_scan();
  std::cout << "[OK] Part-fix done\n";
}

void CgroupController::verify_isolated_cgroup() const {
  const fs::path cgroup_dir = fs::path(kCgroupRoot) / config_.name;

  if (!path_exists(cgroup_dir)) {
    throw CgroupVerifyException("Missing cgroup " + cgroup_dir.string());
  }

  const std::string eff = read_file(cgroup_dir / "cpuset.cpus.effective");
  const std::string part = read_file(cgroup_dir / "cpuset.cpus.partition");

  if (eff.empty() || !sets_equal_str(eff, config_.cpu_range)) {
    throw CgroupVerifyException("Group effective CPUs mismatch: eff=" + eff +
                                " expected=" + config_.cpu_range);
  }

  std::cout << "[OK] Group effective == CPU_RANGE (" << eff << ")\n";

  if (part.starts_with("root") && part.find("invalid") == std::string::npos) {
    std::cout << "[OK] Partition=" << part << "\n";
  } else {
    throw CgroupVerifyException("Partition invalid: " + part);
  }
}

void CgroupController::verify_top_slices() const {
  const std::string present = present_cpus_root();
  if (present.empty()) {
    throw CgroupVerifyException("Cannot read present CPUs");
  }

  const std::string allowed_cpus = subtract_set_str(present, config_.cpu_range);
  if (allowed_cpus.empty()) {
    throw CgroupVerifyException("No allowed CPUs for siblings");
  }

  std::cout << "[INFO] present=" << present
            << " CPU_RANGE=" << config_.cpu_range << " allowed=" << allowed_cpus
            << "\n";

  bool has_errors = false;

  for (const auto& slice : kSystemSlices) {
    const fs::path slice_dir = fs::path(kCgroupRoot) / slice;
    if (!path_exists(slice_dir)) {
      std::cout << "[WARNING] Missing " << slice << "\n";
      continue;
    }

    const std::string eff = read_file(slice_dir / "cpuset.cpus.effective");
    if (eff.empty()) {
      std::cerr << "[FAIL] " << slice << " effective CPUs read failed\n";
      has_errors = true;
      continue;
    }

    if (!sets_equal_str(eff, allowed_cpus)) {
      std::cerr << "[FAIL] " << slice << " eff=" << eff
                << " expected=" << allowed_cpus << "\n";
      has_errors = true;
    } else {
      std::cout << "[OK] " << slice << " eff == allowed (" << eff << ")\n";
    }

    const std::set<int> eff_set = expand_set(eff);
    const std::set<int> range_set = expand_set(config_.cpu_range);

    for (auto cpu : range_set) {
      if (eff_set.contains(cpu)) {
        std::cerr << "[FAIL] " << slice << " overlaps CPU_RANGE\n";
        has_errors = true;
        break;
      }
    }
  }

  if (has_errors) {
    throw CgroupVerifyException("Top slices verification failed");
  }
}