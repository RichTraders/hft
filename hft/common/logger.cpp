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
#include "logger.h"

namespace common {

void ConsoleSink::write(const std::string& msg) {
  std::cout << msg.c_str() << '\n';
}

void FileSink::write(const std::string& msg) {
  if (ofs_.tellp() > static_cast<std::streamoff>(max_size_)) {
    rotate();
  }
  ofs_ << msg << '\n';
}

void FileSink::rotate() {
  ofs_.close();
  const std::string new_file_name =
      filename_ + "_" + std::to_string(++index_) + file_extension_;

  ofs_.open(new_file_name, std::ios::trunc);
}

void Logger::log(LogLevel lvl, const char* file, int line, const char* func,
                 const std::string& text) {
  if (lvl < level_.load(std::memory_order_relaxed))
    return;
  LogMessage msg;
  msg.level = lvl;
  msg.file = file;
  msg.line = line;
  msg.func = func;
  msg.text = text;
  msg.thread_id = std::this_thread::get_id();
  msg.timestamp = "";
  { queue_.enqueue(std::move(msg)); }
}

void Logger::process() {
  while (!stop_) {
    if (stop_.load(std::memory_order_relaxed) && queue_.empty()) {
      break;
    }

    LogMessage msg;
    while (queue_.dequeue(msg)) {
      auto out = LogFormatter::format(msg);
      for (const auto& sink : sinks_) {
        sink->write(out);
      }
    }

    sleep(1);
  }
}
}  // namespace common