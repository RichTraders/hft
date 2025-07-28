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

#ifndef PCH_H
#define PCH_H

#include <algorithm>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <random>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

// 매크로 편의 함수
#define LOG_INFO(logger, text) \
  logger.log(LogLevel::kInfo, __FILE__, __LINE__, __func__, text)

#define LOG_DEBUG(logger, text) \
  logger.log(LogLevel::kDebug, __FILE__, __LINE__, __func__, text)

#define LOG_ERROR(logger, text) \
  logger.log(LogLevel::kError, __FILE__, __LINE__, __func__, text)

#endif  // PCH_H