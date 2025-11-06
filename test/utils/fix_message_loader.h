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

#pragma once

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "hft/common/logger.h"

namespace test {

/**
 * @brief Utility class to load FIX messages from files for testing
 *
 * This loader reads FIX messages from text files where each line contains
 * one complete FIX message. The messages can then be replayed for performance
 * testing without requiring a live server connection.
 */
class FixMessageLoader {
 public:
  explicit FixMessageLoader(common::Logger::Producer logger = {})
      : logger_(std::move(logger)) {}

  /**
   * @brief Load all FIX messages from a file
   * @param file_path Path to the FIX message file (one message per line)
   * @return Vector of FIX message strings
   * @throws std::runtime_error if file cannot be opened or read
   */
  std::vector<std::string> load_messages(const std::string& file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
      const std::string error_msg = "Failed to open FIX message file: " + file_path;
      if (logger_) {
        logger_.error(error_msg);
      }
      throw std::runtime_error(error_msg);
    }

    std::vector<std::string> messages;
    std::string line;
    size_t line_number = 0;

    while (std::getline(file, line)) {
      ++line_number;

      if (line.empty() || line[0] == '#') {
        continue;
      }

      line = trim(line);

      if (!line.empty()) {
        messages.push_back(line);
      }
    }

    if (logger_) {
      logger_.info("Loaded {} FIX messages from {}", messages.size(), file_path);
    }

    if (messages.empty()) {
      if (logger_) {
        logger_.warn("No FIX messages found in file: {}", file_path);
      }
    }

    return messages;
  }

  /**
   * @brief Load a single FIX message from a file
   * @param file_path Path to the FIX message file
   * @param index Index of the message to load (0-based)
   * @return The FIX message string at the specified index
   * @throws std::out_of_range if index is out of bounds
   */
  std::string load_message_at(const std::string& file_path, size_t index) {
    auto messages = load_messages(file_path);
    if (index >= messages.size()) {
      throw std::out_of_range("Message index " + std::to_string(index) +
                              " out of range (total: " + std::to_string(messages.size()) + ")");
    }
    return messages[index];
  }

  /**
   * @brief Load FIX messages and repeat them to reach a target count
   * @param file_path Path to the FIX message file
   * @param target_count Target number of messages
   * @return Vector of FIX message strings (may repeat messages from file)
   */
  std::vector<std::string> load_messages_repeated(const std::string& file_path,
                                                   size_t target_count) {
    auto base_messages = load_messages(file_path);
    if (base_messages.empty()) {
      return {};
    }

    std::vector<std::string> result;
    result.reserve(target_count);

    for (size_t i = 0; i < target_count; ++i) {
      result.push_back(base_messages[i % base_messages.size()]);
    }

    if (logger_) {
      logger_.info("Repeated {} base messages to create {} total messages",
                   base_messages.size(), result.size());
    }

    return result;
  }

 private:
  common::Logger::Producer logger_;

  static std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
      return "";
    }
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
  }
};

}  // namespace test
