//
// Created by neworo2 on 25. 7. 11.
//
#include "logger.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace util;

TEST(LoggerTest, LogTest) {
  auto& lg = Logger::instance();
  lg.setLevel(LogLevel::DEBUG);
  lg.addSink(std::make_unique<ConsoleSink>());

  LOG_INFO("Logger Test");

  int cnt = 0;
  constexpr int thread_cnt = 500;

  auto worker = [&cnt]() {

    for (int i =0; i < thread_cnt; i++) {
      int v = ++cnt;
      LOG_INFO("Loop iteration " + std::to_string(v));
    }
  };


  constexpr int N = 100;
  std::vector<std::thread> threads;

  for (int i = 0; i < N; ++i) {
    std::thread(worker).join();
  }

  LOG_INFO("Application shutting down");

  EXPECT_EQ(cnt, N * thread_cnt);
}
