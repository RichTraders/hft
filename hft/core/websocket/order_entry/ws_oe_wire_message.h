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

#ifndef WS_OE_WIRE_MESSAGE_H
#define WS_OE_WIRE_MESSAGE_H

#include <variant>

#include "schema/spot/response/account_position.h"
#include "schema/spot/response/api_response.h"
#include "schema/spot/response/execution_report.h"
#include "schema/spot/response/order.h"
#include "schema/spot/response/session_response.h"

using WsOeWireMessage = std::variant<std::monostate,
    schema::ExecutionReportResponse, schema::SessionLogonResponse,
    schema::CancelOrderResponse, schema::CancelAllOrdersResponse,
    schema::SessionUserSubscriptionResponse,
    schema::SessionUserUnsubscriptionResponse, schema::CancelAndReorderResponse,
    schema::PlaceOrderResponse, schema::BalanceUpdateEnvelope,
    schema::OutboundAccountPositionEnvelope, schema::ApiResponse>;

#endif  //WS_OE_WIRE_MESSAGE_H
