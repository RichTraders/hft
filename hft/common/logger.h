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
#include <mpsc_queue_cas.hpp>
#include <thread.hpp>

namespace util {
// 로그 레벨 정의
enum class LogLevel : uint8_t { kTrace, kDebug, kInfo, kWarn, kError, kFatal };
enum class PriorityLevel : uint8_t {
  kPercent_100 = 100,
  kPercent_90 = 90,
  kPercent_80 = 80,
  kPercent_70 = 70,
  kPercent_60 = 60,
  kPercent_50 = 50,
  kPercent_40 = 40,
  kPercent_30 = 30,
  kPercent_20 = 20,
  kPercent_10 = 10,
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
class ConsoleSink : public LogSink {
 public:
  ConsoleSink() = default;
  virtual ~ConsoleSink() = default;
  void write(const std::string& msg) override;
};

// 파일 싱크 (회전 기능 지원)
class FileSink : public LogSink {
 public:
  FileSink() = delete;
  FileSink(const std::string& filename, std::size_t max_size)
      : _max_size(max_size),
        _index(0) {
    auto pos = filename.find_last_of('.');
    std::string filename_temp = filename;
    std::string name = (pos == std::string::npos)
                         ? filename                  // 확장자 없음
                         : filename.substr(0, pos);  // “file_name”

    std::string ext  = (pos == std::string::npos)
                         ? ""                        // 확장자 없음
                         : filename.substr(pos);

    _filename = name;
    _file_extension = ext;

    if (_file_extension.empty())
      _file_extension = ".txt";

    _ofs.open(_filename + '_' + std::to_string(_index) + _file_extension, std::ios::app);
  }
  virtual ~FileSink() = default;
  void write(const std::string& msg) override;

 private:
  void rotate();

  std::string _filename;
  std::string _file_extension;
  std::size_t _max_size;
  std::ofstream _ofs;
  int _index;
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

  void setLevel(LogLevel lvl) {
    _level.store(lvl, std::memory_order_relaxed);
  }

  void addSink(std::unique_ptr<LogSink> sink) {
    _sinks.push_back(std::move(sink));
  }

  void log(LogLevel lvl, const char* file, int line, const char* func,
           const std::string& text);

  void clearSink() {
    _sinks.clear();
  }

 private:
  Logger() {
    _stop = static_cast<bool>(_worker.start(&Logger::process, this));
  }

  ~Logger() {
    _stop = true;
    _worker.join();
  }

  void process();

  std::atomic<LogLevel> _level;
  std::vector<std::unique_ptr<LogSink>> _sinks;
  common::MPSCSegQueue<LogMessage, static_cast<int>(QueueChunkSize::kDefaultSize)>
      _queue;
#ifdef UNIT_TEST
  common::Thread<
      common::NormalTag> _worker;
#else
  common::Thread<
      common::PriorityTag<static_cast<int>(PriorityLevel::kPercent_80)>> _worker;
#endif
  std::atomic<bool> _stop;
};

// 매크로 편의 함수
#define LOG_INFO(text) \
  Logger::instance().log(LogLevel::kInfo, __FILE__, __LINE__, __func__, text)

#define LOG_DEBUG(text) \
  Logger::instance().log(LogLevel::kDebug, __FILE__, __LINE__, __func__, text)

#define LOG_ERROR(text) \
  Logger::instance().log(LogLevel::kError, __FILE__, __LINE__, __func__, text)

}  // namespace util