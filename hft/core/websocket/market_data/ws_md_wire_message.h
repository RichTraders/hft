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

#ifndef WS_MD_WIRE_MESSAGE_H
#define WS_MD_WIRE_MESSAGE_H

#include <variant>

#include "schema/response/api_response.h"
#include "schema/response/depth_stream.h"
#include "schema/response/exchange_info_response.h"
#include "schema/response/snapshot.h"
#include "schema/response/trade.h"

using WsMdWireMessage =
    std::variant<std::monostate, schema::DepthResponse, schema::DepthSnapshot,
        schema::TradeEvent, schema::ExchangeInfoResponse, schema::ApiResponse>;

#endif  //WS_MD_WIRE_MESSAGE_H
