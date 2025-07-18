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

namespace util {

void ConsoleSink::write(const std::string& msg) {
  std::cout << msg.c_str() << '\n';
}

void FileSink::write(const std::string& msg) {
  if (_ofs.tellp() > static_cast<std::streamoff>(_max_size)) {
    rotate();
  }
  _ofs << msg << '\n';
}

void FileSink::rotate() {
  _ofs.close();
  const std::string new_file_name = _filename + "_" + std::to_string(++_index) + _file_extension;

  _ofs.open(new_file_name, std::ios::trunc);
}

void Logger::log(LogLevel lvl, const char* file, int line, const char* func,
                 const std::string& text) {
  if (lvl < _level.load(std::memory_order_relaxed))
    return;
  LogMessage msg;
  msg.level = lvl;
  msg.file = file;
  msg.line = line;
  msg.func = func;
  msg.text = text;
  msg.thread_id = std::this_thread::get_id();
  msg.timestamp = "";
  { _queue.enqueue(std::move(msg)); }
}

void Logger::process() {
  while (!_stop) {
    if (_stop.load(std::memory_order_relaxed) && _queue.empty()) {
      break;
    }

    LogMessage msg;
    while (_queue.dequeue(msg)) {
      auto out = LogFormatter::format(msg);
      for (const auto& sink : _sinks) {
        sink->write(out);
      }
    }

    sleep(0.5);
  }
}
}