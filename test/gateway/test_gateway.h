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

#pragma once

#include <string>
#include <vector>

#include "hft/common/logger.h"
#include "hft/core/NewOroFix44/fix_oe_core.h"
#include "hft/src/gateway/gateway_interface.h"

namespace trading {
class ResponseManager;
}

namespace test {

/**
 * @brief Test gateway for performance testing without network I/O
 *
 * This gateway simulates order execution without connecting to a real server,
 * enabling file-based replay of FIX messages for NFR-PF performance tests.
 */
class TestGateway : public trading::IGateway {
 public:
  TestGateway(common::Logger* logger, trading::ResponseManager* response_manager);
  ~TestGateway() override;

  void send_order(const trading::RequestCommon& request) override;
  void stop() override;

  /**
   * @brief Replay execution reports from FIX message file
   * @param messages Vector of FIX message strings (one per line)
   *
   * This method decodes FIX ExecutionReport messages and feeds them to
   * ResponseManager, simulating server responses for performance testing.
   */
  void replay_execution_reports(const std::vector<std::string>& messages);

  /**
   * @brief Enable/disable automatic mock responses
   * @param enabled If true, generates ACK for every order sent
   */
  void set_mock_response_enabled(bool enabled) { mock_responses_ = enabled; }

  /**
   * @brief Get the number of orders sent during the test
   */
  size_t get_order_count() const { return order_count_; }

  /**
   * @brief Reset order counter
   */
  void reset_order_count() { order_count_ = 0; }

 private:
  void generate_mock_ack(const trading::RequestCommon& request);

  trading::ResponseManager* response_manager_;
  common::Logger::Producer logger_;
  std::unique_ptr<core::FixOeCore> decoder_;
  bool mock_responses_{false};
  size_t order_count_{0};
};

}  // namespace test
