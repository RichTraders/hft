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
  std::cout << msg << '\n';
}

void FileSink::write(const std::string& msg) {
  if (ofs_.tellp() > static_cast<std::streamoff>(max_size_)) {
    rotate();
  }
  ofs_ << msg << '\n';

  constexpr uint32_t kMaxLineCnt = 100;

  if (line_cnt_ >= kMaxLineCnt) {
    ofs_.flush();
    line_cnt_ = 0;
  }

  line_cnt_++;
}

void FileSink::rotate() {
  ofs_.close();
  const std::string new_file_name =
      filename_ + "_" + std::to_string(++index_) + file_extension_;

  ofs_.open(new_file_name);
}

void Logger::trace(const std::string& text, const std::source_location& loc) {
  if (level_ > LogLevel::kTrace) {
    return;
  }

  log(LogLevel::kTrace, text, loc);
}

void Logger::debug(const std::string& text, const std::source_location& loc) {
  if (level_ > LogLevel::kDebug) {
    return;
  }

  log(LogLevel::kDebug, text, loc);
}

void Logger::info(const std::string& text, const std::source_location& loc) {
  if (level_ > LogLevel::kInfo) {
    return;
  }

  log(LogLevel::kInfo, text, loc);
}

void Logger::warn(const std::string& text, const std::source_location& loc) {
  if (level_ > LogLevel::kWarn) {
    return;
  }

  log(LogLevel::kWarn, text, loc);
}

void Logger::error(const std::string& text, const std::source_location& loc) {
  if (level_ > LogLevel::kError) {
    return;
  }

  log(LogLevel::kError, text, loc);
}

void Logger::fatal(const std::string& text, const std::source_location& loc) {
  if (level_ > LogLevel::kFatal) {
    return;
  }

  log(LogLevel::kFatal, text, loc);
}

void Logger::log(LogLevel lvl, const std::string& text,
                 const std::source_location& loc) {
  LogMessage msg;

  msg.level = lvl;
  msg.line = loc.line();
  msg.func = loc.function_name();
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

LogLevel Logger::string_to_level(const std::string& level) noexcept {
  if (level == "TRACE")
    return LogLevel::kTrace;
  if (level == "DEBUG")
    return LogLevel::kDebug;
  if (level == "INFO")
    return LogLevel::kInfo;
  if (level == "WARN")
    return LogLevel::kWarn;
  if (level == "ERROR")
    return LogLevel::kError;
  if (level == "FATAL")
    return LogLevel::kFatal;
  return LogLevel::kNone;
}

std::string Logger::level_to_string(LogLevel level) noexcept {
  switch (level) {
    case LogLevel::kTrace:
      return "TRACE";
    case LogLevel::kDebug:
      return "DEBUG";
    case LogLevel::kInfo:
      return "INFO";
    case LogLevel::kWarn:
      return "WARN";
    case LogLevel::kError:
      return "ERROR";
    case LogLevel::kFatal:
      return "FATAL";
    default:
      return "NONE";
  }
}

}  // namespace common