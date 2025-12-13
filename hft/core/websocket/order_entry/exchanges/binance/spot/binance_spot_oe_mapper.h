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

#ifndef BINANCE_SPOT_OE_MAPPER_H
#define BINANCE_SPOT_OE_MAPPER_H

#include "common/logger.h"
#include "core/response_manager.h"
#include "schema/spot/response/api_response.h"
#include "schema/spot/response/execution_report.h"

namespace core {

class BinanceSpotOeMapper {
 public:
  using WireExecutionReport = schema::ExecutionReportResponse;
  using WireCancelReject = schema::ExecutionReportResponse;
  using WireMassCancelReport = schema::ExecutionReportResponse;
  using WireReject = schema::ApiResponse;

  BinanceSpotOeMapper(const common::Logger::Producer& logger,
      trading::ResponseManager* response_manager)
      : logger_(logger), response_manager_(response_manager) {}

  [[nodiscard]] trading::ExecutionReport* to_execution_report(
      const WireExecutionReport& msg) const;
  [[nodiscard]] trading::OrderCancelReject* to_cancel_reject(
      const WireCancelReject& msg) const;
  [[nodiscard]] trading::OrderMassCancelReport* to_mass_cancel_report(
      const WireMassCancelReport& msg) const;
  [[nodiscard]] trading::OrderReject to_reject(const WireReject& msg) const;

 private:
  [[nodiscard]] trading::ExecutionReport* allocate_execution_report() const;
  [[nodiscard]] trading::OrderCancelReject* allocate_cancel_reject() const;
  [[nodiscard]] trading::OrderMassCancelReport* allocate_mass_cancel_report()
      const;

  const common::Logger::Producer& logger_;
  trading::ResponseManager* response_manager_;
};

}  // namespace core

#include "binance_spot_oe_mapper.tpp"

#endif  // BINANCE_SPOT_OE_MAPPER_H
