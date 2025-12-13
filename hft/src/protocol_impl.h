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

#ifndef PROTOCOL_IMPL_H
#define PROTOCOL_IMPL_H

#ifdef ENABLE_WEBSOCKET
#include "core/websocket/market_data/ws_md_app.h"
#include "core/websocket/order_entry/ws_oe_app.h"
namespace protocol_impl {
using OrderEntryApp = core::WsOrderEntryApp;
using MarketDataApp = core::WsMarketDataApp;
}  // namespace protocol_impl
#else
#include "core/fix/fix_md_app.h"
#include "core/fix/fix_oe_app.h"
namespace protocol_impl {
using OrderEntryApp = core::FixOrderEntryApp;
using MarketDataApp = core::FixMarketDataApp;
}  // namespace protocol_impl
#endif

#endif  //PROTOCOL_IMPL_H
