/*
 * MIT License
 * 
 * Copyright (c) 2025 NewOro Corporation
 * 
 * Permission is hereby granted, free of charge, to use, copy, modify, and distribute 
 * this software for any purpose with or without fee, provided that the above 
 * copyright notice appears in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 */

#include "pid_notifier.hpp"

#include <gtest/gtest.h>
#include <thread>
#include <fstream>
#include <filesystem>
#include <future>

namespace fs = std::filesystem;
using namespace common;

class PidNotifierTest : public ::testing::Test {
protected:
  void SetUp() override {
    fifo_path = fs::temp_directory_path() / "pid_notifier_test.fifo";

    if (fs::exists(fifo_path)) {
      fs::remove(fifo_path);
    }

    if (::mkfifo(fifo_path.c_str(), 0600) != 0) {
      throw std::runtime_error("mkfifo failed");
    }
  }

  void TearDown() override {
    fs::remove(fifo_path);
  }

  fs::path fifo_path;
};

TEST_F(PidNotifierTest, WritesPidToFifo) {
  PidNotifier notifier(fifo_path.string());

  std::promise<std::string> line_promise;
  auto line_future = line_promise.get_future();

  std::thread reader([&] {
      int fd = ::open(fifo_path.c_str(), O_RDONLY);
      ASSERT_GE(fd, 0);

      char buf[64] = {0};
      ssize_t n = ::read(fd, buf, sizeof(buf) - 1);
      ASSERT_GT(n, 0);

      ::close(fd);
      line_promise.set_value(std::string(buf, static_cast<size_t>(n)));
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  ASSERT_TRUE(notifier.notify_now());

  auto line = line_future.get();
  reader.join();

  pid_t mypid = ::getpid();
  std::string expected = std::to_string(mypid) + "\n";

  EXPECT_EQ(line, expected);
}
