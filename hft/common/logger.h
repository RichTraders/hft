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

#pragma once

#include <pch.h>
#include "mpsc_queue_cas.hpp"
#include "thread.hpp"

namespace common {
// 로그 레벨 정의
enum class LogLevel : uint8_t { kTrace, kDebug, kInfo, kWarn, kError, kFatal };
enum class PriorityLevel : uint8_t {
  kPercent100 = 100,
  kPercent90 = 90,
  kPercent80 = 80,
  kPercent70 = 70,
  kPercent60 = 60,
  kPercent50 = 50,
  kPercent40 = 40,
  kPercent30 = 30,
  kPercent20 = 20,
  kPercent10 = 10,
};
enum class QueueChunkSize : uint16_t {
  kDefaultSize = 64,
  kSmallSize = 128,
  kMidSize = 512,
  kBigSize = 1024,
};

// 로그 메시지 구조체
struct LogMessage {
  LogLevel level;
  std::string timestamp;
  std::thread::id thread_id;
  std::string file;
  int line;
  std::string func;
  std::string text;
};

// 로그 싱크 인터페이스
class LogSink {
 public:
  virtual ~LogSink() = default;
  virtual void write(const std::string& msg) = 0;
};

// 콘솔 싱크
class ConsoleSink final : public LogSink {
 public:
  ConsoleSink() = default;
  void write(const std::string& msg) override;
};

// 파일 싱크 (회전 기능 지원)
class FileSink final : public LogSink {
 public:
  FileSink() = delete;
  FileSink(const std::string& filename, std::size_t max_size)
      : max_size_(max_size), index_(0) {
    auto pos = filename.find_last_of('.');
    const std::string name = (pos == std::string::npos)
                                 ? filename                  // 확장자 없음
                                 : filename.substr(0, pos);  // “file_name”

    const std::string ext = (pos == std::string::npos) ? ""  // 확장자 없음
                                                       : filename.substr(pos);

    filename_ = name;
    file_extension_ = ext;

    if (file_extension_.empty())
      file_extension_ = ".txt";

    ofs_.open(filename_ + '_' + std::to_string(index_) + file_extension_,
              std::ios::app);
  }
  void write(const std::string& msg) override;

 private:
  void rotate();

  std::string filename_;
  std::string file_extension_;
  std::size_t max_size_;
  std::ofstream ofs_;
  int index_;
};

// 포맷터
class LogFormatter {
 public:
  static std::string format(const LogMessage& msg) {
    std::ostringstream oss;
    auto now = std::chrono::system_clock::now();
    auto ttime = std::chrono::system_clock::to_time_t(now);
    auto milisec = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now.time_since_epoch() % std::chrono::seconds(1))
                       .count();
    std::tm ttm;
    localtime_r(&ttime, &ttm);
    oss << "[" << std::put_time(&ttm, "%Y-%m-%dT%H:%M:%S") << "."
        << std::setw(3) << std::setfill('0') << milisec << "]";
    oss << "[" << levelToString(msg.level) << "]";
    oss << "[tid=" << msg.thread_id << "]";
    oss << "[" << msg.file << ":" << msg.line << "]";
    oss << "[" << msg.func << "] ";
    oss << msg.text;
    return oss.str();
  }

 private:
  static const char* levelToString(LogLevel lvl) {
    switch (lvl) {
      case LogLevel::kTrace:
        return "Trace";
      case LogLevel::kDebug:
        return "Debug";
      case LogLevel::kInfo:
        return "Info";
      case LogLevel::kWarn:
        return "Warn";
      case LogLevel::kError:
        return "Error";
      case LogLevel::kFatal:
        return "Fatal";
    }
    return "UNKNOWN";
  }
};

class Logger {
 public:
  static Logger& instance() {
    static Logger inst;
    return inst;
  }

  void setLevel(LogLevel lvl) { level_.store(lvl, std::memory_order_relaxed); }

  void addSink(std::unique_ptr<LogSink> sink) {
    sinks_.push_back(std::move(sink));
  }

  void log(LogLevel lvl, const char* file, int line, const char* func,
           const std::string& text);

  void clearSink() { sinks_.clear(); }

 private:
  Logger() { stop_ = static_cast<bool>(worker_.start(&Logger::process, this)); }

  ~Logger() {
    stop_ = true;
    worker_.join();
  }

  void process();

  std::atomic<LogLevel> level_;
  std::vector<std::unique_ptr<LogSink>> sinks_;
  MPSCSegQueue<LogMessage, static_cast<int>(QueueChunkSize::kDefaultSize)>
      queue_;
#ifdef UNIT_TEST
  Thread<NormalTag> worker_;
#else
  Thread<PriorityTag<static_cast<int>(PriorityLevel::kPercent80)>> worker_;
#endif
  std::atomic<bool> stop_;
};

// 매크로 편의 함수
#define LOG_INFO(text) \
  Logger::instance().log(LogLevel::kInfo, __FILE__, __LINE__, __func__, text)

#define LOG_DEBUG(text) \
  Logger::instance().log(LogLevel::kDebug, __FILE__, __LINE__, __func__, text)

#define LOG_ERROR(text) \
  Logger::instance().log(LogLevel::kError, __FILE__, __LINE__, __func__, text)

}  // namespace common