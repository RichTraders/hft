//
// Created by neworo2 on 25. 7. 11.
//

#pragma once

#include <pch.h>
#include <thread.hpp>
#include <mpsc_queue_cas.hpp>

#define PRIORITY_LEVEL 80

namespace util {
// 로그 레벨 정의
enum class LogLevel { TRACE, DEBUG, INFO, WARN, ERROR, FATAL };

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
    void write(const std::string& msg) override;
private:
    std::mutex mutex_;
};

// 파일 싱크 (회전 기능 지원)
class FileSink : public LogSink {
public:
    FileSink(const std::string& filename, std::size_t max_size)
        : filename_(filename), max_size_(max_size), ofs_(filename, std::ios::app)
    {}

    void write(const std::string& msg) override;

private:
    void rotate();

    std::string filename_;
    std::size_t max_size_;
    std::ofstream ofs_;
    std::mutex mutex_;
};

// 포맷터
class LogFormatter {
public:
    static std::string format(const LogMessage& msg) {
        std::ostringstream oss;
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch() % std::chrono::seconds(1)
        ).count();
        std::tm tm;
        localtime_r(&t, &tm);
        oss << "[" << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S")
            << "." << std::setw(3) << std::setfill('0') << ms << "]";
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
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
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
        level_.store(lvl, std::memory_order_relaxed);
    }

    void addSink(std::unique_ptr<LogSink> sink) {
        sinks_.push_back(std::move(sink));
    }

    void log(LogLevel lvl, const char* file, int line, const char* func, const std::string& text);

private:
    Logger() {
        stop_ = worker_.start(&Logger::process, this);
    }
    ~Logger() {
        stop_ = true;
        worker_.join();
    }

    void process();

    std::atomic<LogLevel> level_;
    std::vector<std::unique_ptr<LogSink>> sinks_;
    MPSCSegQueue<LogMessage, 64> queue_;
    common::Thread<common::PriorityTag<PRIORITY_LEVEL>> worker_;
    std::counting_semaphore<INT_MAX> sem_{0};
    std::atomic<bool> stop_;
};

// 매크로 편의 함수
#define LOG_INFO(text) \
    Logger::instance().log(LogLevel::INFO, __FILE__, __LINE__, __func__, text)

#define LOG_DEBUG(text) \
    Logger::instance().log(LogLevel::DEBUG, __FILE__, __LINE__, __func__, text)

#define LOG_ERROR(text) \
    Logger::instance().log(LogLevel::ERROR, __FILE__, __LINE__, __func__, text)

}