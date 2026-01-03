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

#ifndef COMMON_LOGGER_H
#define COMMON_LOGGER_H

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

#include "performance.h"
#include "thread.hpp"

namespace common {
#ifdef LOGGER_PERF_TRACE

struct LogPerfSample {
  uint64_t format_cycles;   // vformat
  uint64_t enqueue_cycles;  // clock_gettime + enqueue
  uint64_t total_cycles;    // 전체 hot path
};

struct LogPerfStats {
  static constexpr size_t kMaxSamples = 100'000;
  std::array<LogPerfSample, kMaxSamples> samples;
  std::atomic<size_t> count{0};

  void record(uint64_t format, uint64_t enqueue, uint64_t total) noexcept {
    const size_t idx = count.fetch_add(1, std::memory_order_relaxed);
    if (idx < kMaxSamples) {
      samples[idx] = {format, enqueue, total};
    }
  }

  void dump(const char* filename = "logger_perf.csv") const {
    const size_t size =
        std::min(count.load(std::memory_order_relaxed), kMaxSamples);
    FILE* file = fopen(filename, "w");
    if (!file)
      return;
    fprintf(file, "format_cycles,enqueue_cycles,total_cycles\n");
    for (size_t i = 0; i < size; ++i) {
      fprintf(file,
          "%lu,%lu,%lu\n",
          samples[i].format_cycles,
          samples[i].enqueue_cycles,
          samples[i].total_cycles);
    }
    fclose(file);
    printf("[LogPerfStats] dumped %zu samples to %s\n", size, filename);
  }

  void summary() const {
    const size_t size =
        std::min(count.load(std::memory_order_relaxed), kMaxSamples);
    if (size == 0)
      return;
    uint64_t fmt_sum = 0;
    uint64_t enq_sum = 0;
    uint64_t tot_sum = 0;
    uint64_t fmt_max = 0;
    uint64_t enq_max = 0;
    uint64_t tot_max = 0;
    for (size_t i = 0; i < size; ++i) {
      fmt_sum += samples[i].format_cycles;
      enq_sum += samples[i].enqueue_cycles;
      tot_sum += samples[i].total_cycles;
      fmt_max = std::max(fmt_max, samples[i].format_cycles);
      enq_max = std::max(enq_max, samples[i].enqueue_cycles);
      tot_max = std::max(tot_max, samples[i].total_cycles);
    }
    printf("[LogPerfStats] samples=%zu\n", size);
    printf("  format:  avg=%lu, max=%lu cycles\n", fmt_sum / size, fmt_max);
    printf("  enqueue: avg=%lu, max=%lu cycles\n", enq_sum / size, enq_max);
    printf("  total:   avg=%lu, max=%lu cycles\n", tot_sum / size, tot_max);
  }
};

inline LogPerfStats g_log_perf_stats;

#endif  // LOGGER_PERF_TRACE

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
    out.append(msg.text);
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
    auto time_p = std::chrono::time_point<std::chrono::system_clock,
        std::chrono::nanoseconds>(std::chrono::nanoseconds(ts_ns));
    const auto sec = std::chrono::time_point_cast<std::chrono::seconds>(time_p);
    const auto nano =
        std::chrono::duration_cast<std::chrono::nanoseconds>(time_p - sec)
            .count();

    const std::time_t time = std::chrono::system_clock::to_time_t(sec);
    std::tm calendar_date;
    gmtime_r(&time, &calendar_date);

    const auto* time_format = std::format_to(out,
        "[{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:06}Z]",
        calendar_date.tm_year + k1900,
        calendar_date.tm_mon + 1,
        calendar_date.tm_mday,
        calendar_date.tm_hour,
        calendar_date.tm_min,
        calendar_date.tm_sec,
        nano / k1000);
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

    template <typename... Args>
    void logf(LogLevel lvl, std::string_view fmt,
        Args&&... args) const noexcept {
      if (!is_enabled(lvl))
        return;

#ifdef LOGGER_PERF_TRACE
      const auto time0 = rdtsc();
#endif

      try {
        const std::string formatted =
            std::vformat(fmt, std::make_format_args(args...));

#ifdef LOGGER_PERF_TRACE
        const auto time1 = rdtsc();
#endif

        log(lvl, formatted);

#ifdef LOGGER_PERF_TRACE
        const auto time2 = rdtsc();
        g_log_perf_stats.record(time1 - time0, time2 - time1, time2 - time0);
#endif

      } catch (const std::format_error&) {
        std::cout << "[Critical]Parser error\n";
      } catch (const std::bad_alloc&) {
        std::cout << "[Critical]memory allocation error\n";
      } catch (const std::exception& exception) {
        std::cout << "[Critical]exception : " << exception.what() << "\n";
      }
    }

    template <typename... Args>
    void info(std::string_view fmt, Args&&... args) const {
      logf(LogLevel::kInfo, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void debug(std::string_view fmt, Args&&... args) const {
      logf(LogLevel::kDebug, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void trace(std::string_view fmt, Args&&... args) const {
      logf(LogLevel::kTrace, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void warn(std::string_view fmt, Args&&... args) const {
      logf(LogLevel::kWarn, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void error(std::string_view fmt, Args&&... args) const {
      logf(LogLevel::kError, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void fatal(std::string_view fmt, Args&&... args) const {
      logf(LogLevel::kFatal, fmt, std::forward<Args>(args)...);
    }

    void log(LogLevel lvl, std::string_view text,
        std::source_location loc = std::source_location::current()) const;

    void info(std::string_view str,
        std::source_location loc = std::source_location::current()) const {
      log(LogLevel::kInfo, str, loc);
    }
    void debug(std::string_view str,
        std::source_location loc = std::source_location::current()) const {
      log(LogLevel::kDebug, str, loc);
    }
    void trace(std::string_view str,
        std::source_location loc = std::source_location::current()) const {
      log(LogLevel::kTrace, str, loc);
    }
    void warn(std::string_view str,
        std::source_location loc = std::source_location::current()) const {
      log(LogLevel::kWarn, str, loc);
    }
    void error(std::string_view str,
        std::source_location loc = std::source_location::current()) const {
      log(LogLevel::kError, str, loc);
    }
    void fatal(std::string_view str,
        std::source_location loc = std::source_location::current()) const {
      log(LogLevel::kFatal, str, loc);
    }

   private:
    struct Impl;  // per-producer (ProducerToken 보유)
    Impl* impl_{nullptr};
    explicit Producer(Impl* producer) : impl_(producer) {}
    friend class Logger;
    [[nodiscard]] bool is_enabled(LogLevel lvl) const noexcept;
  };

 private:
  void process() const;
  void dispatch(const LogMessage& msg) const;

  std::atomic<LogLevel> level_{LogLevel::kInfo};
  std::vector<std::unique_ptr<LogSink>> sinks_;

  struct Impl;
  std::unique_ptr<Impl> impl_;

  Thread<"Logger"> worker_;
  std::atomic<bool> stop_{false};
};

}  // namespace common
#endif