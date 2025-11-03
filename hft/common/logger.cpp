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
#include "concurrentqueue.h"
#include "wait_strategy.h"

namespace common {
static constexpr int kDrainLimit = 4096;

using moodycamel::ConcurrentQueue;
using moodycamel::ConsumerToken;
using moodycamel::ProducerToken;

void ConsoleSink::write(const std::string& msg) {
  std::cout << msg << '\n';
}

void FileSink::write(const std::string& msg) {
  const auto cur = ofs_.tellp();
  if (cur >= 0 && static_cast<size_t>(cur) + msg.size() + 1 > max_size_) {
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
  ofs_.flush();
  ofs_.close();
  const std::string new_file_name =
      filename_ + "_" + std::to_string(++index_) + file_extension_;
  std::filesystem::rename(filename_ + file_extension_, new_file_name);

  ofs_.open(filename_ + file_extension_);
}

void FileSink::reopen_fallback() {
  std::error_code error_code;
  ofs_.flush();
  ofs_.close();
  std::filesystem::rename(filename_ + file_extension_,
                          filename_ + "_reopen_" +
                              std::to_string(std::time(nullptr)) +
                              file_extension_,
                          error_code);
  ofs_.open(filename_ + file_extension_);
  ofs_.exceptions(std::ofstream::failbit | std::ofstream::badbit);
  line_cnt_ = 0;
}

struct Logger::Impl {
  ConcurrentQueue<LogMessage> queue;

  std::atomic<bool> stopping{false};
  std::atomic<size_t> next_sid{0};
};

struct Logger::Producer::Impl {
  Logger::Impl* logger;
  std::atomic<LogLevel>* level;
  size_t sid;

  explicit Impl(Logger::Impl* impl, std::atomic<LogLevel>* lvl, size_t sid_)
      : logger(impl), level(lvl), sid(sid_) {}

  ~Impl() = default;
};

Logger::Logger() : impl_(std::make_unique<Impl>()) {
  stop_ = static_cast<bool>(worker_.start(&Logger::process, this));
  level_ = LogLevel::kInfo;
}

Logger::~Logger() noexcept {
  try {
    shutdown();

    for (auto& sink : sinks_) {
      if (auto* file_sink = dynamic_cast<FileSink*>(sink.get())) {
        file_sink->flush();
      }
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "Logger dtor suppressed: %s\n", e.what());
  }
}

Logger::Producer Logger::make_producer() {
  const auto sid = impl_->next_sid.fetch_add(1, std::memory_order_relaxed);
  auto* pip = new Producer::Impl(impl_.get(), &level_, sid);
  Producer producer;
  producer.impl_ = pip;
  return producer;
}

void Logger::shutdown() {
  bool expected = false;
  if (!stop_.compare_exchange_strong(expected, true,
                                     std::memory_order_acq_rel)) {
    return;
  }

  impl_->queue.enqueue(LogMessage::make_stop_sentinel());
  worker_.join();

  LogMessage msg;
  ConsumerToken drain_ct{impl_->queue};
  while (impl_->queue.try_dequeue(drain_ct, msg)) {
    if (!msg.is_stop()) {
      const auto out = LogFormatter::format(msg);
      if (out.empty())
        continue;
      for (const auto& sink : sinks_)
        sink->write(out);
    }
  }

  for (auto& sink : sinks_) {
    if (auto* file = dynamic_cast<FileSink*>(sink.get()))
      file->flush();
  }
}
void Logger::flush() {
  for (auto& sink : sinks_) {
    if (auto* file = dynamic_cast<FileSink*>(sink.get()))
      file->flush();
  }
}

void Logger::dispatch(const LogMessage& msg) const {
  for (const auto& sink : sinks_)
    sink->write(msg.text);
}

void Logger::process() const {
  WaitStrategy wait_strategy;
  ConsumerToken consumer_token{impl_->queue};

  bool stopping = false;
  while (!stopping) {
    size_t drained = 0;
    LogMessage msg;

    while (drained < kDrainLimit &&
           impl_->queue.try_dequeue(consumer_token, msg)) {
      if (msg.is_stop()) {
        stopping = true;
        break;
      }

      const auto out = LogFormatter::format(msg);
      if (out.empty())
        continue;

      for (const auto& sink : sinks_)
        sink->write(out);
      ++drained;
    }

    if (!stopping) {
      if (drained == 0)
        wait_strategy.idle();
      else
        wait_strategy.reset();
    }
  }

  LogMessage rest;
  while (impl_->queue.try_dequeue(consumer_token, rest)) {
    if (!rest.is_stop()) {
      const auto out = LogFormatter::format(rest);
      for (const auto& sink : sinks_)
        sink->write(out);
    }
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

Logger::Producer::~Producer() {
  delete impl_;
  impl_ = nullptr;
}
Logger::Producer::Producer(Producer&& producer) noexcept
    : impl_(producer.impl_) {
  producer.impl_ = nullptr;
}

Logger::Producer& Logger::Producer::operator=(Producer&& producer) noexcept {
  if (this != &producer) {
    delete impl_;
    impl_ = producer.impl_;
    producer.impl_ = nullptr;
  }
  return *this;
}

void Logger::Producer::log(LogLevel lvl, std::string_view text,
                           std::source_location /*loc*/) {
  if (impl_->level->load(std::memory_order_relaxed) > lvl)
    return;

  //Allow multiple logger
  thread_local std::unordered_map<const void*,
                                  std::vector<std::unique_ptr<ProducerToken>>>
      tls;
  auto& slots = tls[impl_->logger];
  if (slots.size() <= impl_->sid)
    slots.resize(impl_->sid + 1);
  if (!slots[impl_->sid])
    slots[impl_->sid] = std::make_unique<ProducerToken>(impl_->logger->queue);

  LogMessage msg;
  msg.level = lvl;
  // msg.line = loc.line();
  // msg.func = loc.function_name();
  // msg.thread_id = std::this_thread::get_id();

  timespec time;
  clock_gettime(CLOCK_REALTIME, &time);
  const uint64_t ts_ns = static_cast<uint64_t>(time.tv_sec) * 1'000'000'000ULL +
                         static_cast<uint64_t>(time.tv_nsec);

  msg.ts_ns = ts_ns;
  msg.text = text;

  impl_->logger->queue.enqueue(*slots[impl_->sid], std::move(msg));
}
}  // namespace common