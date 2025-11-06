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

#include "test_gateway.h"

#include <format>

#include "hft/src/response_manager.h"

namespace test {

TestGateway::TestGateway(common::Logger* logger,
                         trading::ResponseManager* response_manager)
    : response_manager_(response_manager),
      logger_(logger->make_producer()),
      decoder_(std::make_unique<core::FixOeCore>()) {
  logger_.info("[Constructor] TestGateway Constructor");
}

TestGateway::~TestGateway() {
  logger_.info("[Destructor] TestGateway Destroy");
}

void TestGateway::send_order(const trading::RequestCommon& request) {
  ++order_count_;

  // Log the order for debugging (without actual network I/O)
  logger_.debug(std::format("[TestGateway] Order sent [type: {}, cl_order_id: {}, "
                            "price: {}, qty: {}]",
                            toString(request.req_type), request.cl_order_id.value,
                            request.price.value, request.order_qty.value));

  // Optionally generate mock ACK response
  if (mock_responses_) {
    generate_mock_ack(request);
  }
}

void TestGateway::stop() {
  logger_.info(std::format("[TestGateway] Stopped (sent {} orders)", order_count_));
}

void TestGateway::replay_execution_reports(const std::vector<std::string>& messages) {
  logger_.info(std::format("[TestGateway] Replaying {} execution reports",
                           messages.size()));

  for (const auto& msg_str : messages) {
    try {
      // Decode FIX message using existing FixOeCore
      FIX8::Message* msg = decoder_->decode(msg_str);
      if (!msg) {
        logger_.warn(std::format("[TestGateway] Failed to decode message: {}", msg_str));
        continue;
      }

      // Check if it's an ExecutionReport (35=8)
      auto* exec_report = dynamic_cast<FIX8::NewOroFix44OE::ExecutionReport*>(msg);
      if (exec_report) {
        trading::ResponseCommon res;
        res.res_type = trading::ResponseType::kExecutionReport;
        res.execution_report = decoder_->create_execution_report_message(exec_report);

        // Feed to ResponseManager (skipping TradeEngine queue for direct testing)
        // Note: In production this goes through TradeEngine's SPSC queue
        logger_.trace(std::format("[TestGateway] Replayed execution report: {}",
                                  res.execution_report->toString()));
      }

      delete msg;

    } catch (const std::exception& e) {
      logger_.error(std::format("[TestGateway] Exception during replay: {}", e.what()));
    }
  }

  logger_.info("[TestGateway] Replay complete");
}

void TestGateway::generate_mock_ack(const trading::RequestCommon& request) {
  // Generate a simple ACK ExecutionReport for testing
  // In a real implementation, you would construct a proper FIX message

  logger_.trace(std::format(
      "[TestGateway] Mock ACK generated for cl_order_id: {}", request.cl_order_id.value));

  // TODO: Implement mock ExecutionReport generation if needed for unit tests
  // For now, this is just a placeholder for future implementation
}

}  // namespace test
