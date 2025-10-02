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

#include <cerrno>
#include <filesystem>
#include <regex>
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "logger.h"

using namespace common;

inline std::string now_iso8601_ns() {
  using namespace std::chrono;
  const auto now = system_clock::now();

  const auto sec_tp = time_point_cast<seconds>(now);
  const auto nsec = duration_cast<nanoseconds>(now - sec_tp).count();

  std::time_t tt = system_clock::to_time_t(sec_tp);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &tt);
#else
  localtime_r(&tt, &tm);
#endif

  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(9)
      << std::setfill('0') << nsec;  // 나노초 9자리
  return oss.str();
}

std::string getCurrentWorkingDirectory() {
  char buf[PATH_MAX];
  if (!::getcwd(buf, sizeof(buf)))
    throw std::runtime_error("getcwd failed");
  return std::string(buf);
}

TEST(LoggerTest, ConsoleLogTest) {
  Logger lg;
  lg.setLevel(LogLevel::kDebug);
  lg.clearSink();
  lg.addSink(std::make_unique<ConsoleSink>());

  std::ostringstream oss;
  auto* old_cout_buf = std::cout.rdbuf(oss.rdbuf());

  std::vector<std::string> logs;

  logs.emplace_back("Logger Test");
  logs.emplace_back("Application shutting down");

  for (const auto& log : logs) {
    lg.debug(log);
  }
  sleep(2);

  // 3) std::cout 버퍼 복원
  std::cout.rdbuf(old_cout_buf);

  // 4) 캡처된 문자열을 istringstream 에 담아 std::cin 으로 연결
  std::istringstream iss(oss.str());
  auto* old_cin_buf = std::cin.rdbuf(iss.rdbuf());

  // 5) 이제 std::cin 으로 읽으면 oss 에 담긴 내용이 들어온다
  std::string line;
  int cnt = 0;
  while (std::getline(std::cin, line)) {
    EXPECT_TRUE(line.find(logs[cnt++]) != std::string::npos);
  }

  // 6) std::cin 버퍼도 복원
  std::cin.rdbuf(old_cin_buf);
}

TEST(LoggerTest, FileLogTest) {
  //init
  std::string cur_dir_path = getCurrentWorkingDirectory();
  std::string remove_dir = cur_dir_path + "/file_log_test.txt";

  std::ifstream remove_file(remove_dir);

  if (remove_file.is_open())
    std::remove(remove_dir.c_str());

  Logger lg;
  lg.setLevel(LogLevel::kDebug);
  lg.clearSink();
  lg.addSink(std::make_unique<FileSink>("file_log_test", 1024));

  std::vector<std::string> line_list;
  line_list.push_back("FileLogTest Test");
  line_list.push_back("Application shutting down333");

  lg.debug(line_list[0]);
  lg.debug(line_list[1]);

  sleep(2);

  const std::string file_path = "file_log_test.txt";
  std::ifstream ifs(file_path);

  EXPECT_TRUE(ifs.is_open());

  for (uint i = 0; i < line_list.size(); i++) {
    std::string line;
    if (std::getline(ifs, line)) {
      EXPECT_TRUE(line.find(line_list[i]) != std::string::npos);
    }
  }
}

TEST(LoggerTest, FileLogLotateTest) {
  const std::string cur_dir_path = getCurrentWorkingDirectory();
  const std::string file_path = "file_log_rotate_test_final.txt";
  const std::string file_path2 = "file_log_rotate_test_final_1.txt";
  const std::string remove_dir1 = cur_dir_path + file_path;
  const std::string remove_dir2 = cur_dir_path + file_path2;

  std::ifstream remove_file(remove_dir1);

  if (remove_file.is_open())
    std::remove(remove_dir1.c_str());

  std::ifstream remove_file2(remove_dir2);

  if (remove_file2.is_open())
    std::remove(remove_dir2.c_str());

  std::vector<std::string> line_list;
  line_list.push_back("FileLogTest rotate Test");
  line_list.push_back("Application rotate shutting down333");

  Logger lg;

  lg.setLevel(LogLevel::kDebug);
  lg.clearSink();
  lg.addSink(std::make_unique<FileSink>("file_log_rotate_test_final", 32));

  lg.debug(line_list[0].c_str());
  lg.debug(line_list[1].c_str());
  sleep(3);
  {
    std::ifstream ifs(file_path);

    EXPECT_TRUE(ifs.is_open());

    std::string line;
    if (std::getline(ifs, line)) {
      int ssss = line.find(line_list[0]) != std::string::npos;
      EXPECT_TRUE(ssss);
    }

    ifs.close();
  }

  {
    std::ifstream ifs(file_path);

    EXPECT_TRUE(ifs.is_open());

    std::string line;
    if (std::getline(ifs, line)) {
      int ssss = line.find(line_list[1]) != std::string::npos;
      EXPECT_TRUE(ssss);
    }
    ifs.close();
  }
  {
    std::ifstream ifs(file_path2);

    EXPECT_TRUE(ifs.is_open());

    std::string line;
    if (std::getline(ifs, line)) {
      int ssss = line.find(line_list[0]) != std::string::npos;
      EXPECT_TRUE(ssss);
    }
    ifs.close();
  }
}

TEST(LoggerTest, FileAndConsoleLogTest) {
  std::string cur_dir_path = getCurrentWorkingDirectory();
  std::string remove_dir = cur_dir_path + "/file_console_file_log_test.txt";

  std::ifstream remove_file(remove_dir);

  if (remove_file.is_open())
    std::remove(remove_dir.c_str());

  Logger lg;
  lg.setLevel(LogLevel::kDebug);
  lg.clearSink();
  lg.addSink(std::make_unique<ConsoleSink>());
  lg.addSink(std::make_unique<FileSink>("file_console_file_log_test", 1024));

  std::ostringstream oss;
  auto* old_cout_buf = std::cout.rdbuf(oss.rdbuf());

  std::vector<std::string> logs;

  logs.emplace_back("FileAndConsoleLogTest Test");
  logs.emplace_back("FileAndConsoleLogTest shutting down");

  for (const auto& log : logs) {
    lg.debug(log);
  }
  sleep(3);

  std::cout.rdbuf(old_cout_buf);

  std::istringstream iss(oss.str());
  auto* old_cin_buf = std::cin.rdbuf(iss.rdbuf());

  std::string line;
  int cnt = 0;
  while (std::getline(std::cin, line)) {
    EXPECT_TRUE(line.find(logs[cnt]) != std::string::npos);
    cnt++;
  }

  std::cin.rdbuf(old_cin_buf);

  const std::string file_path = "file_console_file_log_test.txt";
  std::ifstream ifs(file_path);

  EXPECT_TRUE(ifs.is_open());

  for (uint i = 0; i < logs.size(); i++) {
    std::string line;
    if (std::getline(ifs, line)) {
      EXPECT_TRUE(line.find(logs[i]) != std::string::npos);
    }
  }
}

TEST(LoggerTest, LogLevelTest) {
  Logger lg;
  lg.setLevel(LogLevel::kInfo);
  lg.clearSink();
  lg.addSink(std::make_unique<FileSink>("LogLevelTest", 1024 * 1024));

  for (int i = 0; i < 200; i++) {
    lg.debug("info LogLevelTest" + std::to_string(i));
  }

  sleep(3);

  const std::string file_path = "LogLevelTest.txt";
  std::ifstream ifs(file_path);
  EXPECT_TRUE(ifs.is_open());
  std::string line;
  std::getline(ifs, line);
  EXPECT_TRUE(line.empty());
}

namespace fs = std::filesystem;
using namespace std::chrono_literals;
using common::FileSink;
using common::Logger;
using common::LogLevel;
using common::LogSink;

// ---- 유틸: 로그 파일 라인수 카운트 & 정리 ----
uint64_t count_lines_in_file(const fs::path& f) {
  std::ifstream ifs(f);
  if (!ifs)
    return 0;
  uint64_t lines = 0;
  std::string s;
  while (std::getline(ifs, s))
    ++lines;
  return lines;
}

std::vector<fs::path> list_log_files(const fs::path& dir,
                                     const std::string& stem,
                                     const std::string& ext) {
  std::vector<fs::path> out;
  if (!fs::exists(dir))
    return out;
  std::regex rx("^" + stem + "(?:_\\d+)?" + ext + "$");
  for (auto& p : fs::directory_iterator(dir)) {
    if (!p.is_regular_file())
      continue;
    const auto name = p.path().filename().string();
    if (std::regex_match(name, rx))
      out.push_back(p.path());
  }
  std::sort(out.begin(), out.end());
  return out;
}

uint64_t count_lines_all(const fs::path& dir, const std::string& stem,
                         const std::string& ext) {
  uint64_t tot = 0;
  for (auto& f : list_log_files(dir, stem, ext))
    tot += count_lines_in_file(f);
  return tot;
}

void remove_log_files(const fs::path& dir, const std::string& stem,
                      const std::string& ext) {
  for (auto& f : list_log_files(dir, stem, ext)) {
    std::error_code ec;
    fs::remove(f, ec);
  }
}

// ---- 검증용 CountingSink ----
class CountingSink final : public LogSink {
 public:
  void write(const std::string& msg) override {
    (void)msg;
    count_.fetch_add(1, std::memory_order_relaxed);
  }
  uint64_t count() const { return count_.load(std::memory_order_relaxed); }

 private:
  std::atomic<uint64_t> count_{0};
};

// ---- 임시 디렉토리 ----
struct TempDir {
  fs::path path;

  static std::string make_suffix() {
    // 타임스탬프 + tid 해시 + 랜덤값 섞어서 충돌 확률 낮춤
    const auto now =
        std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const auto tid = std::hash<std::thread::id>{}(std::this_thread::get_id());
    static thread_local std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<uint64_t> dist;
    std::ostringstream oss;
    oss << std::hex << now << "-" << tid << "-" << dist(rng);
    return oss.str();
  }

  TempDir() {
    const fs::path base = fs::temp_directory_path();
    constexpr int kMaxAttempts = 20;
    std::error_code ec;

    for (int i = 0; i < kMaxAttempts; ++i) {
      fs::path candidate = base / fs::path("logger_stress_" + make_suffix());
      if (fs::create_directories(candidate, ec)) {
        path = std::move(candidate);
        return;
      }
    }
    throw std::runtime_error(
        "TempDir: failed to create a unique temp directory");
  }

  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

TEST(LoggerStress, ConcurrentWriteAndRotationLineCount) {
  TempDir tmp;
  const std::string stem = "stress";
  const std::string ext = ".log";
  const fs::path base = tmp.path / (stem + ext);

  // 혹시 남아있을 잔여 파일 제거
  remove_log_files(tmp.path, stem, ext);

  const int kThreads = 8;
  const int kMsgsPerThread = 50000;        // 총 40만 줄
  const size_t kRotateBytes = 512 * 1024;  // 512KB로 자주 회전 유도

  auto* counting_raw = new CountingSink();
  uint64_t count_from_sink = 0;
  {
    Logger logger;
    logger.setLevel(LogLevel::kTrace);
    logger.addSink(std::make_unique<FileSink>(base.string(), kRotateBytes));

    std::unique_ptr<LogSink> counting_uptr(counting_raw);
    logger.addSink(std::move(counting_uptr));

    // 멀티 스레드 로깅
    std::atomic<int> started{0};
    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
      workers.emplace_back([&, t] {
        started.fetch_add(1, std::memory_order_relaxed);
        // 각 메시지를 유니크하게 (파일 검사 시 디버깅 편의)
        for (int i = 0; i < kMsgsPerThread; ++i) {
          logger.info("T=" + std::to_string(t) + " i=" + std::to_string(i));
        }
      });
    }
    // 모두 시작될 때까지 대기(짧게)
    while (started.load() < kThreads)
      std::this_thread::sleep_for(1ms);
    for (auto& th : workers)
      th.join();

    const uint64_t expected = static_cast<uint64_t>(kThreads) * kMsgsPerThread;

    // 워커 스레드가 큐를 비우는 시간을 조금 준다.
    // (네 현재 구현은 sleep(1) 등이 있어 드레인에 시간이 걸릴 수 있음)
    const auto t0 = std::chrono::steady_clock::now();
    while (counting_raw->count() < expected &&
           std::chrono::steady_clock::now() - t0 < 10s) {
      std::this_thread::sleep_for(5us);
    }

    // 아직 덜 썼다면, 이 시점에서 테스트가 실패할 가능성 ↑
    // (그 자체로 버그 신호: 종료 전에 큐를 다 못 비움)
    EXPECT_EQ(counting_raw->count(), expected)
        << "Sink에 전달된 라인 수가 기대치와 다름";
    count_from_sink = counting_raw->count();
  }

  // 파일 라인 수 집계
  uint64_t file_lines = count_lines_all(tmp.path, stem, ext);

  // 파일에 실제로 쓴 라인 수 == Sink가 받은 라인 수
  EXPECT_EQ(file_lines, count_from_sink)
      << "파일 총 라인수와 CountingSink 수 불일치 (로테이션/flush/종료 경로 "
         "점검 필요)";

  // 로테이션이 실제로 발생했는지(파일이 1개 초과)
  auto files = list_log_files(tmp.path, stem, ext);
  EXPECT_GE(files.size(), 2u) << "로테이션이 발생하지 않음";
}

TEST(LoggerStress, ConcurrentWriteAndRotationLineCountWithNoCounter) {
  TempDir tmp;
  const std::string stem = "stress";
  const std::string ext = ".log";
  const fs::path base = tmp.path / (stem + ext);

  remove_log_files(tmp.path, stem, ext);

  const int kThreads = 8;
  const int kMsgsPerThread = 50000;
  const size_t kRotateBytes = 512 * 1024;
  const uint64_t expected = uint64_t(kThreads) * kMsgsPerThread;

  {
    Logger logger;
    logger.setLevel(LogLevel::kTrace);
    logger.addSink(std::make_unique<FileSink>(base.string(), kRotateBytes));

    // 멀티스레드 생산
    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
      workers.emplace_back([&, t] {
        for (int i = 0; i < kMsgsPerThread; ++i) {
          logger.info("ts=" + now_iso8601_ns() + ", T=" + std::to_string(t) + " i=" + std::to_string(i));
        }
      });
    }
    for (auto& th : workers)
      th.join();

    // 여기서 스코프 종료되면 ~Logger(): 워커 join + FileSink flush/close
  }

  // 파일 라인 수 집계 (메타데이터 지연 대비 재시도)
  uint64_t file_lines = 0;
  for (int i = 0; i < 10; ++i) {
    file_lines = count_lines_all(tmp.path, stem, ext);
    if (file_lines == expected)
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  EXPECT_EQ(file_lines, expected);
  auto files = list_log_files(tmp.path, stem, ext);
  EXPECT_GE(files.size(), 2u);  // 로테이션 확인
}
