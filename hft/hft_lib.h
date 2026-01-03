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

#ifndef HFT_LIB_H
#define HFT_LIB_H

#include "common/cpumanager/cpu_manager.h"
#include "common/spsc_queue.h"
#include "core/response_manager.h"
#include "ini_config.hpp"
#include "logger.h"
#include "market_consumer.hpp"
#include "order_entry.h"
#include "order_gateway.hpp"
#include "risk_manager.h"
#include "trade_engine.hpp"

#ifdef ENABLE_WEBSOCKET
#include "core/websocket/market_data/ws_md_app.hpp"
#include "core/websocket/order_entry/ws_oe_app.hpp"
using SelectedMarketApp = core::WsMarketDataApp;
using SelectedOrderApp = core::WsOrderEntryApp;
#else
#include "core/fix/fix_md_app.h"
#include "core/fix/fix_oe_app.h"
using SelectedMarketApp = core::FixMarketDataApp;
using SelectedOrderApp = core::FixOrderEntryApp;
#endif

#endif  // HFT_LIB_H