//
// Created by neworo2 on 25. 7. 11.
//

#include "logger.h"

using namespace util;

void ConsoleSink::write(const std::string& msg) {
  std::cout << msg << std::endl;
}

void FileSink::write(const std::string& msg) {
  ofs_ << msg << std::endl;
  if (ofs_.tellp() > static_cast<std::streamoff>(max_size_)) {
    rotate();
  }
}

void FileSink::rotate() {
  ofs_.close();
  std::string backup = filename_ + ".1";
  std::remove(backup.c_str());
  std::rename(filename_.c_str(), backup.c_str());
  ofs_.open(filename_, std::ios::trunc);
}


void Logger::log(LogLevel lvl,
             const char* file, int line,
             const char* func, const std::string& text) {
  if (lvl < level_.load(std::memory_order_relaxed)) return;
  LogMessage msg;
  msg.level = lvl;
  msg.file = file;
  msg.line = line;
  msg.func = func;
  msg.text = text;
  msg.thread_id = std::this_thread::get_id();
  msg.timestamp = ""; // timestamp generated in formatter
  {
    queue_.push(std::move(msg));
  }
  sem_.release();
}


void Logger::process() {
  while (!stop_) {

    sem_.acquire();

    // 2) 종료 플래그 & 큐 비어있으면 종료
    if (stop_.load(std::memory_order_relaxed) && queue_.empty()) {
      break;
    }

    LogMessage msg;
    while (queue_.pop(msg)) {
      auto out = LogFormatter::format(msg);
      for (auto& s : sinks_) {
        s->write(out);
      }
    }
  }
}