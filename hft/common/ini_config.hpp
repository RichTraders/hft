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

#include "singleton.h"

namespace common {
class IniConfig : public Singleton<IniConfig> {
 public:
  bool load(const std::string& filename) {
    std::ifstream file(filename);
    if (!file)
      return false;

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

  // NOLINTBEGIN(bugprone-easily-swappable-parameters,-warnings-as-errors)
  std::string get(const std::string_view section, const std::string_view key,
                  const std::string_view def = "") const {
    const std::string full_key = std::string(section) + "." + std::string(key);
    if (const auto iter = data_.find(full_key); iter != data_.end()) {
      return iter->second;
    }
    return std::string(def);
  }

  // NOLINTEND(bugprone-easily-swappable-parameters,-warnings-as-errors)

  int get_int(const std::string_view section, const std::string_view key,
              const int def = 0) const {
    try {
      return std::stoi(get(section, key));
    } catch (...) {
      return def;
    }
  }

  uint64_t get_uint64_t(const std::string_view section,
                        const std::string_view key, const int def = 0) const {
    try {
      return std::stoull(get(section, key));
    } catch (...) {
      return def;
    }
  }

  double get_double(const std::string_view section, const std::string_view key,
                    const double def = 0.0) const {
    try {
      return std::stod(get(section, key));
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

  static void trim(std::string& str) {
    auto not_space = [](unsigned char character) {
      return std::isspace(character) == 0;
    };
    str.erase(str.begin(), std::ranges::find_if(str, not_space));
    str.erase(std::find_if(str.rbegin(), str.rend(), not_space).base(),
              str.end());
  }
};
}  // namespace common

#define INI_CONFIG common::IniConfig::instance()

#endif  //INI_READER_H