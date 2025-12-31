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

#ifndef INI_READER_H
#define INI_READER_H

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "singleton.h"

namespace common {
class IniConfig : public Singleton<IniConfig> {
 public:
  bool load(const std::string& filename) {
    data_.clear();
    loaded_files_.clear();

    if (!load_single_file(filename)) {
      return false;
    }

    base_path_ = std::filesystem::path(filename).parent_path();
    load_profiles();

    return true;
  }

  [[nodiscard]] bool has_key(std::string_view section,
      std::string_view key) const {
    const std::string full_key = std::string(section) + "." + std::string(key);
    return data_.contains(full_key);
  }

  [[nodiscard]] std::string get_active_symbol() const {
    return active_profiles_.symbol;
  }

  [[nodiscard]] std::string get_active_strategy() const {
    return active_profiles_.strategy;
  }

  [[nodiscard]] std::string get_active_environment() const {
    return active_profiles_.environment;
  }

  [[nodiscard]] const std::vector<std::string>& get_loaded_files() const {
    return loaded_files_;
  }

  void dump_all() const {
    for (const auto& [key, value] : data_) {
      std::cout << key << " = " << value << "\n";
    }
  }

  // NOLINTBEGIN(bugprone-easily-swappable-parameters,-warnings-as-errors)
  void set(const std::string& section, const std::string& key,
      const std::string& value) {
    const std::string full_key = std::string(section) + "." + std::string(key);
    data_[full_key] = value;
  }
  [[nodiscard]] std::string get(const std::string_view section,
      const std::string_view key, const std::string_view def = "") const {
    const std::string full_key = std::string(section) + "." + std::string(key);
    if (const auto iter = data_.find(full_key); iter != data_.end()) {
      return iter->second;
    }
    return std::string(def);
  }

  [[nodiscard]] std::string get_with_symbol(const std::string_view section,
      const std::string_view key, const std::string_view def = "") const {
    std::string value = get(section, key, def);
    if (value.empty() || active_profiles_.symbol.empty()) {
      return value;
    }

    std::string lower_symbol = active_profiles_.symbol;
    std::ranges::transform(lower_symbol,
        lower_symbol.begin(),
        [](unsigned char chr) { return std::tolower(chr); });

    static constexpr std::string_view kPlaceholder = "{symbol}";
    size_t pos = 0;
    while ((pos = value.find(kPlaceholder, pos)) != std::string::npos) {
      value.replace(pos, kPlaceholder.size(), lower_symbol);
      pos += lower_symbol.size();
    }
    return value;
  }

  // NOLINTEND(bugprone-easily-swappable-parameters,-warnings-as-errors)
  int get_int(const std::string_view section, const std::string_view key,
      const int def = 0) const {
    try {
      auto value = get(section, key);
      std::erase(value, '\'');
      std::erase(value, ',');
      return std::stoi(value);
    } catch (...) {
      return def;
    }
  }

  uint64_t get_uint64_t(const std::string_view section,
      const std::string_view key, const int def = 0) const {
    try {
      auto value = get(section, key);
      std::erase(value, '\'');
      std::erase(value, ',');
      return std::stoull(value);
    } catch (...) {
      return def;
    }
  }

  int64_t get_int64(const std::string_view section,
      const std::string_view key, const int64_t def = 0) const {
    try {
      auto value = get(section, key);
      std::erase(value, '\'');
      std::erase(value, ',');
      return std::stoll(value);
    } catch (...) {
      return def;
    }
  }

  double get_double(const std::string_view section, const std::string_view key,
      const double def = 0.0) const {
    try {
      auto value = get(section, key);
      std::erase(value, '\'');
      std::erase(value, ',');
      return std::stod(value);
    } catch (...) {
      return def;
    }
  }

  float get_float(const std::string_view section, const std::string_view key,
      const float def = 0.0) const {
    try {
      return std::stof(get(section, key));
    } catch (...) {
      return def;
    }
  }

 private:
  std::unordered_map<std::string, std::string> data_;

  struct ProfileInfo {
    std::string environment;
    std::string symbol;
    std::string strategy;
  };
  ProfileInfo active_profiles_;
  std::vector<std::string> loaded_files_;
  std::filesystem::path base_path_;

  bool load_single_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file)
      return false;

    loaded_files_.push_back(filename);

    const std::string content((std::istreambuf_iterator(file)),
        std::istreambuf_iterator<char>());

    std::string current_section;
    std::istringstream iss(content);
    std::string line;

    while (std::getline(iss, line)) {
      trim(line);
      if (line.empty() || line[0] == ';' || line[0] == '#')
        continue;

      if (line.front() == '[' && line.back() == ']') {
        current_section = line.substr(1, line.size() - 2);
        trim(current_section);
      } else {
        if (const auto pos = line.find('='); pos != std::string::npos) {
          std::string key = line.substr(0, pos);
          std::string value = line.substr(pos + 1);
          trim(key);
          trim(value);

          const std::string full_key =
              current_section.empty()
                  ? key
                  : std::format("{}.{}", current_section, key);
          data_[full_key] = value;
        }
      }
    }
    return true;
  }

  void load_profiles() {
    active_profiles_.environment = get("profile", "environment", "");
    active_profiles_.symbol = get("profile", "symbol", "");
    active_profiles_.strategy = get("profile", "strategy", "");

    static constexpr size_t kProfilePrefixLength = 8;  // "profile."
    std::vector<std::pair<std::string, std::string>> custom_profiles;
    for (const auto& [key, value] : data_) {
      if (key.starts_with("profile.")) {
        const std::string profile_type = key.substr(kProfilePrefixLength);
        if (profile_type != "environment" && profile_type != "symbol" &&
            profile_type != "strategy") {
          custom_profiles.emplace_back(profile_type, value);
        }
      }
    }

    std::ranges::sort(custom_profiles,
        [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

    // Load order: env → custom profiles → symbol → strategy
    if (!active_profiles_.environment.empty()) {
      load_profile_file("env", active_profiles_.environment);
    }

    for (const auto& [type, name] : custom_profiles) {
      load_profile_file(type, name);
    }

    if (!active_profiles_.symbol.empty()) {
      load_profile_file("symbol", active_profiles_.symbol);
    }

    if (!active_profiles_.strategy.empty()) {
      load_profile_file("strategy", active_profiles_.strategy);
    }
  }

  void load_profile_file(const std::string& profile_type,
      const std::string& profile_name) {
    const std::string filename =
        resolve_profile_path(profile_type, profile_name);

    if (!std::filesystem::exists(filename)) {
      return;
    }

    std::unordered_map<std::string, std::string> temp_data;

    std::ifstream file(filename);
    if (!file)
      return;

    loaded_files_.push_back(filename);

    const std::string content((std::istreambuf_iterator(file)),
        std::istreambuf_iterator<char>());

    std::string current_section;
    std::istringstream iss(content);
    std::string line;

    while (std::getline(iss, line)) {
      trim(line);
      if (line.empty() || line[0] == ';' || line[0] == '#')
        continue;

      if (line.front() == '[' && line.back() == ']') {
        current_section = line.substr(1, line.size() - 2);
        trim(current_section);
      } else {
        if (const auto pos = line.find('='); pos != std::string::npos) {
          std::string key = line.substr(0, pos);
          std::string value = line.substr(pos + 1);
          trim(key);
          trim(value);

          const std::string full_key =
              current_section.empty()
                  ? key
                  : std::format("{}.{}", current_section, key);
          temp_data[full_key] = value;
        }
      }
    }

    // Merge: overwrite existing values
    for (const auto& [key, value] : temp_data) {
      data_[key] = value;
    }
  }

  [[nodiscard]] std::string resolve_profile_path(
      const std::string& profile_type, const std::string& profile_name) const {
    // resources/symbol/config-BTCUSDT.ini
    // resources/strategy/config-maker.ini
    // resources/env/config-prod.ini
    return (base_path_ / profile_type / ("config-" + profile_name + ".ini"))
        .string();
  }

  static void trim(std::string& str) {
    auto not_space = [](unsigned char character) {
      return std::isspace(character) == 0;
    };
    str.erase(str.begin(), std::ranges::find_if(str, not_space));
    str.erase(
        std::ranges::find_if(std::ranges::reverse_view(str), not_space).base(),
        str.end());
  }
};
}  // namespace common

#define INI_CONFIG common::IniConfig::instance()

#endif  //INI_READER_H