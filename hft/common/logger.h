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

#include <source_location>
#include "thread.hpp"

namespace common {

enum class LogLevel : uint8_t {
  kTrace,
  kDebug,
  kInfo,
  kWarn,
  kError,
  kFatal,
  kNone
};

enum class QueueChunkSize : uint16_t {
  kDefaultSize = 64,
  kSmallSize = 128,
  kMidSize = 512,
  kBigSize = 1024,
};

struct LogMessage {
  enum class Kind : uint8_t { kNormal, kStop };

  Kind kind{Kind::kNormal};
  LogLevel level{};
  //uint32_t line = 0;
  //std::string func;
  //std::thread::id thread_id;
  uint64_t ts_ns = 0;
  std::string text;

  static LogMessage make_stop_sentinel() {
    LogMessage msg;
    msg.kind = Kind::kStop;
    return msg;
  }
  [[nodiscard]] bool is_stop() const noexcept { return kind == Kind::kStop; }
};

class LogSink {
 public:
  virtual ~LogSink() = default;
  virtual void write(const std::string& msg) = 0;
};

class ConsoleSink final : public LogSink {
 public:
  ConsoleSink() = default;
  void write(const std::string& msg) override;
};

class FileSink final : public LogSink {
 public:
  FileSink() = delete;
  FileSink(const std::string& filename, std::size_t max_size)
      : max_size_(max_size) {
    auto pos = filename.find_last_of('.');
    const std::string name =
        (pos == std::string::npos) ? filename : filename.substr(0, pos);

    const std::string ext =
        (pos == std::string::npos) ? "" : filename.substr(pos);

    filename_ = name;
    file_extension_ = ext;

    if (file_extension_.empty())
      file_extension_ = ".txt";
    ofs_.open(filename_ + file_extension_);
  }
  void write(const std::string& msg) override;

  void flush() { ofs_.flush(); }

 private:
  void rotate();
  void reopen_fallback();

  std::string filename_;
  std::string file_extension_;
  std::size_t max_size_;
  std::ofstream ofs_;
  uint32_t line_cnt_{0};
  int index_{0};
};

class LogFormatter {
 public:
  static std::string format(const LogMessage& msg) {
    if (msg.is_stop())
      return {};

    std::string out;

#ifndef LOGGER_PREFIX_DISABLED
    out.reserve(kBufferSize + msg.text.size());
    std::array<char, kBufferSize> buf;
    size_t blen = 0;
    format_iso8601_utc(buf.data(), blen, msg.ts_ns);
    out.append(buf.data(), blen);
#else
    out.reserve(msg.text.size());
#endif
    std::format_to(std::back_inserter(out), "{}", msg.text);
    return out;
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
      default:
        return "Unknown";
    }
  }

  static void format_iso8601_utc(char* out, size_t& len, uint64_t ts_ns) {
    const auto time_p = std::chrono::time_point<std::chrono::system_clock>(
        std::chrono::nanoseconds(ts_ns));
    const auto sec = time_point_cast<std::chrono::seconds>(time_p);
    const auto nano =
        std::chrono::duration_cast<std::chrono::nanoseconds>(time_p - sec)
            .count();

    const std::time_t time = std::chrono::system_clock::to_time_t(sec);
    std::tm calendar_date;
    gmtime_r(&time, &calendar_date);

    const auto* time_format = std::format_to(
        out, "[{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:06}Z]",
        calendar_date.tm_year + k1900, calendar_date.tm_mon + 1,
        calendar_date.tm_mday, calendar_date.tm_hour, calendar_date.tm_min,
        calendar_date.tm_sec, nano / k1000);
    len = static_cast<size_t>(time_format - out);
  }
  static constexpr int kTimeDigit = 6;
  static constexpr int k1900 = 1900;
  static constexpr int k1000 = 1000;
  static constexpr int kBufferSize = 64;
};

class Logger {
 public:
  class Producer;

  Logger();
  ~Logger() noexcept;

  void setLevel(LogLevel lvl) { level_.store(lvl, std::memory_order_relaxed); }

  void addSink(std::unique_ptr<LogSink> sink) {
    sinks_.push_back(std::move(sink));
  }
  void clearSink() { sinks_.clear(); }
  static LogLevel string_to_level(const std::string& level) noexcept;
  static std::string level_to_string(LogLevel level) noexcept;
  void shutdown();
  void flush();

  Producer make_producer();

  class Producer {
   public:
    Producer() = default;
    Producer(Producer&&) noexcept;
    Producer& operator=(Producer&&) noexcept;
    ~Producer();

    explicit operator bool() const noexcept { return impl_ != nullptr; }

    void log(LogLevel lvl, std::string_view text,
             std::source_location loc = std::source_location::current());

    void info(std::string_view str,
              std::source_location loc = std::source_location::current()) {
      log(LogLevel::kInfo, str, loc);
    }
    void debug(std::string_view str,
               std::source_location loc = std::source_location::current()) {
      log(LogLevel::kDebug, str, loc);
    }
    void trace(std::string_view str,
               std::source_location loc = std::source_location::current()) {

      log(LogLevel::kTrace, str, loc);
    }
    void warn(std::string_view str,
              std::source_location loc = std::source_location::current()) {
      log(LogLevel::kWarn, str, loc);
    }
    void error(std::string_view str,
               std::source_location loc = std::source_location::current()) {
      log(LogLevel::kError, str, loc);
    }
    void fatal(std::string_view str,
               std::source_location loc = std::source_location::current()) {
      log(LogLevel::kFatal, str, loc);
    }

   private:
    struct Impl;  // per-producer (ProducerToken 보유)
    Impl* impl_{nullptr};
    explicit Producer(Impl* producer) : impl_(producer) {}
    friend class Logger;
  };

 private:
  void process() const;
  void dispatch(const LogMessage& msg) const;

  std::atomic<LogLevel> level_;
  std::vector<std::unique_ptr<LogSink>> sinks_;

  struct Impl;
  std::unique_ptr<Impl> impl_;

  Thread<"Logger"> worker_;
  std::atomic<bool> stop_{false};
};

}  // namespace common