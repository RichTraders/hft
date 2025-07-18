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
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include <cerrno>

using namespace util;

std::string getCurrentWorkingDirectory() {
  char buf[PATH_MAX];
  if (!::getcwd(buf, sizeof(buf)))
    throw std::runtime_error("getcwd failed");
  return std::string(buf);
}

TEST(LoggerTest, ConsoleLogTest) {
  auto& lg = Logger::instance();
  lg.setLevel(LogLevel::kDebug);
  lg.clearSink();
  lg.addSink(std::make_unique<ConsoleSink>());

  std::ostringstream oss;
  auto* old_cout_buf = std::cout.rdbuf(oss.rdbuf());

  std::vector<std::string> logs;

  logs.emplace_back("Logger Test");
  logs.emplace_back("Application shutting down");

  for (const auto& log : logs) {
    LOG_INFO(log);
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
#include <cstdio>
#include <cstring>
#include <iostream>

TEST(LoggerTest, FileLogTest) {
  std::string cur_dir_path = getCurrentWorkingDirectory();
  std::string remove_dir = cur_dir_path + "/file_log_test_0.txt";
  std::remove(remove_dir.c_str());

  auto& lg = Logger::instance();
  lg.setLevel(LogLevel::kDebug);
  lg.clearSink();
  lg.addSink(std::make_unique<FileSink>("file_log_test", 1024));

  std::vector<std::string> line_list;
  line_list.push_back("FileLogTest Test");
  line_list.push_back("Application shutting down333");

  LOG_INFO(line_list[0].c_str());
  LOG_INFO(line_list[1].c_str());

  sleep(2);

  const std::string file_path = "file_log_test_0.txt";
  std::ifstream ifs(file_path);

  EXPECT_TRUE(ifs.is_open());

  for (uint i =0;i < line_list.size();i++) {
    std::string line;
    if (std::getline(ifs, line)) {
      EXPECT_TRUE(line.find(line_list[i]) != std::string::npos);
    }
  }
}

TEST(LoggerTest, FileLogLotateTest) {
  std::string cur_dir_path = getCurrentWorkingDirectory();
  std::string remove_dir1 = cur_dir_path + "/file_log_lotate_test_0.txt";
  std::string remove_dir2 = cur_dir_path + "/file_log_lotate_test_1.txt";

  std::remove(remove_dir1.c_str());
  std::remove(remove_dir2.c_str());

  std::vector<std::string> line_list;
  line_list.push_back("FileLogTest lotate Test");
  line_list.push_back("Application lotate shutting down333");
  const std::string file_path = "file_log_lotate_test_0.txt";
  const std::string file_path2 = "file_log_lotate_test_1.txt";

  {
    auto& lg = Logger::instance();
    lg.setLevel(LogLevel::kDebug);
    lg.clearSink();
    lg.addSink(std::make_unique<FileSink>("file_log_lotate_test", 32));


    LOG_INFO(line_list[0].c_str());
    LOG_INFO(line_list[1].c_str());
  }

  sleep(2);

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
    std::ifstream ifs(file_path2);

    EXPECT_TRUE(ifs.is_open());

    std::string line;
    if (std::getline(ifs, line)) {
      int ssss = line.find(line_list[1]) != std::string::npos;
      EXPECT_TRUE(ssss);
    }
  }
}

TEST(LoggerTest, FileAndConsoleLogTest) {
  std::string cur_dir_path = getCurrentWorkingDirectory();
  std::string remove_dir = cur_dir_path + "/file_console_file_log_test_0.txt";

  std::remove(remove_dir.c_str());

  auto& lg = Logger::instance();
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
    LOG_INFO(log);
  }
  sleep(3);

  // 3) std::cout 버퍼 복원
  std::cout.rdbuf(old_cout_buf);

  // 4) 캡처된 문자열을 istringstream 에 담아 std::cin 으로 연결
    std::istringstream iss(oss.str());
  auto* old_cin_buf = std::cin.rdbuf(iss.rdbuf());

  // 5) 이제 std::cin 으로 읽으면 oss 에 담긴 내용이 들어온다
  std::string line;
  int cnt = 0;
  while (std::getline(std::cin, line)) {
    EXPECT_TRUE(line.find(logs[cnt]) != std::string::npos);
    cnt++;
  }

  // 6) std::cin 버퍼도 복원
  std::cin.rdbuf(old_cin_buf);

  const std::string file_path = "file_console_file_log_test_0.txt";
  std::ifstream ifs(file_path);

  EXPECT_TRUE(ifs.is_open());

  for (uint i =0;i < logs.size();i++) {
    std::string line;
    if (std::getline(ifs, line)) {
      EXPECT_TRUE(line.find(logs[i]) != std::string::npos);
    }
  }
}
