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

#ifndef FILE_TRANSPORT_H
#define FILE_TRANSPORT_H

#include "global.h"

#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace core {

enum class ReplayMode : uint8_t { kInstant, kRealtime };

template <FixedString ThreadName>
class FileTransport {
 public:
  using MessageCallback = std::function<void(std::string_view)>;

  FileTransport() = default;

  FileTransport(const std::string& /*host*/, int /*port*/,
      const std::string& /*path*/ = "/", bool /*use_ssl*/ = true,
      bool notify_connected = false, std::string_view /*api_key*/ = "")
      : notify_connected_(notify_connected) {}

  ~FileTransport() = default;

  FileTransport(const FileTransport&) = delete;
  FileTransport& operator=(const FileTransport&) = delete;

  void initialize(const std::string& /*host*/, int /*port*/,
      const std::string& /*path*/ = "/", bool /*use_ssl*/ = true,
      bool notify_connected = false, std::string_view /*api_key*/ = "") {
    notify_connected_ = notify_connected;
    connected_ = true;
    if (notify_connected_ && callback_) {
      callback_("__CONNECTED__");
    }
  }

  void register_message_callback(MessageCallback callback) {
    callback_ = std::move(callback);
    if (notify_connected_ && connected_ && callback_) {
      callback_("__CONNECTED__");
    }
  }

  int write(const std::string& buffer) const {
    if (!connected_ || interrupted_) {
      return -1;
    }
    std::lock_guard<std::mutex> lock(write_mutex_);
    sent_messages_.push_back(buffer);
    return static_cast<int>(buffer.size());
  }

  void interrupt() {
    interrupted_ = true;
    connected_ = false;
  }

  bool load_file(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
      return false;
    }
    std::string content((std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    if (content.empty()) {
      return false;
    }
    add_message(std::move(content));
    return true;
  }

  bool load_jsonl(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
      return false;
    }
    std::string line;
    while (std::getline(file, line)) {
      if (!line.empty() && line[0] != '#') {
        add_message(std::move(line));
      }
    }
    return true;
  }

  bool load_directory(const std::string& dir) {
    if (!std::filesystem::exists(dir)) {
      return false;
    }
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
      if (entry.path().extension() == ".json" ||
          entry.path().extension() == ".jsonl") {
        files.push_back(entry.path());
      }
    }
    std::sort(files.begin(), files.end());
    for (const auto& path : files) {
      if (path.extension() == ".jsonl") {
        load_jsonl(path.string());
      } else {
        load_file(path.string());
      }
    }
    return !files.empty();
  }

  void set_replay_mode(ReplayMode mode) { mode_ = mode; }

  void set_replay_speed(double multiplier) { speed_ = multiplier; }

  bool replay_next() {
    TimedMessage msg;
    {
      std::lock_guard<std::mutex> lock(replay_mutex_);
      if (replay_queue_.empty()) {
        return false;
      }
      msg = std::move(const_cast<TimedMessage&>(replay_queue_.top()));
      replay_queue_.pop();
    }

    if (mode_ == ReplayMode::kRealtime && last_timestamp_ > 0 && speed_ > 0) {
      auto delta = static_cast<double>(msg.timestamp - last_timestamp_);
      auto delay =
          std::chrono::milliseconds(static_cast<uint64_t>(delta / speed_));
      std::this_thread::sleep_for(delay);
    }
    last_timestamp_ = msg.timestamp;

    if (callback_) {
      callback_(msg.payload);
    }
    return true;
  }

  void replay_all() {
    while (replay_next()) {}
  }

  [[nodiscard]] size_t pending_count() const {
    std::lock_guard<std::mutex> lock(replay_mutex_);
    return replay_queue_.size();
  }

  void inject_message(std::string_view payload) {
    if (callback_) {
      callback_(payload);
    }
  }

  void inject_from_file(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
      throw std::runtime_error("Cannot open test file: " + filepath);
    }
    std::string content((std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    inject_message(content);
  }

  void queue_message(const std::string& payload) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    queue_.push_back(payload);
  }

  bool deliver_next() {
    std::string msg;
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      if (queue_.empty()) {
        return false;
      }
      msg = std::move(queue_.front());
      queue_.pop_front();
    }
    inject_message(msg);
    return true;
  }

  void deliver_all() {
    while (deliver_next()) {}
  }

  void simulate_connect() {
    connected_ = true;
    interrupted_ = false;
    if (notify_connected_ && callback_) {
      callback_("__CONNECTED__");
    }
  }

  void simulate_disconnect() { connected_ = false; }

  [[nodiscard]] bool is_connected() const {
    return connected_ && !interrupted_;
  }

  [[nodiscard]] std::vector<std::string> get_sent_messages() const {
    std::lock_guard<std::mutex> lock(write_mutex_);
    return sent_messages_;
  }

  [[nodiscard]] std::string get_last_sent_message() const {
    std::lock_guard<std::mutex> lock(write_mutex_);
    return sent_messages_.empty() ? "" : sent_messages_.back();
  }

  [[nodiscard]] size_t sent_message_count() const {
    std::lock_guard<std::mutex> lock(write_mutex_);
    return sent_messages_.size();
  }

  void clear_sent_messages() {
    std::lock_guard<std::mutex> lock(write_mutex_);
    sent_messages_.clear();
  }

  [[nodiscard]] size_t queue_size() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return queue_.size();
  }

  void reset() {
    std::lock_guard<std::mutex> lock1(write_mutex_);
    std::lock_guard<std::mutex> lock2(queue_mutex_);
    std::lock_guard<std::mutex> lock3(replay_mutex_);
    sent_messages_.clear();
    queue_.clear();
    while (!replay_queue_.empty()) {
      replay_queue_.pop();
    }
    connected_ = false;
    interrupted_ = false;
    notify_connected_ = false;
    last_timestamp_ = 0;
  }

 private:
  static constexpr int kDecimalBase = 10;

  struct TimedMessage {
    uint64_t timestamp;
    std::string payload;
    bool operator>(const TimedMessage& other) const {
      return timestamp > other.timestamp;
    }
  };

  static uint64_t extract_timestamp(std::string_view payload) {
    auto data_pos = payload.find("\"data\"");
    if (data_pos == std::string_view::npos) {
      data_pos = 0;
    }
    auto e_pos = payload.find("\"E\":", data_pos);
    if (e_pos == std::string_view::npos) {
      return 0;
    }
    e_pos += 4;
    while (e_pos < payload.size() && payload[e_pos] == ' ') {
      ++e_pos;
    }
    uint64_t result = 0;
    while (e_pos < payload.size() && payload[e_pos] >= '0' &&
           payload[e_pos] <= '9') {
      result = result * kDecimalBase + (payload[e_pos] - '0');
      ++e_pos;
    }
    return result;
  }

  void add_message(std::string_view payload) {
    uint64_t timestamp = extract_timestamp(payload);
    std::lock_guard<std::mutex> lock(replay_mutex_);
    replay_queue_.push(TimedMessage{timestamp, std::string(payload)});
  }

  MessageCallback callback_;
  bool notify_connected_{false};
  bool connected_{false};
  bool interrupted_{false};

  mutable std::mutex write_mutex_;
  mutable std::vector<std::string> sent_messages_;

  mutable std::mutex queue_mutex_;
  std::deque<std::string> queue_;

  mutable std::mutex replay_mutex_;
  std::priority_queue<TimedMessage, std::vector<TimedMessage>,
      std::greater<TimedMessage>>
      replay_queue_;
  ReplayMode mode_ = ReplayMode::kInstant;
  double speed_ = 1.0;
  uint64_t last_timestamp_ = 0;
};

}  // namespace core

#endif  // FILE_TRANSPORT_H
