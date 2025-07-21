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

#include "trade_engine.h"
constexpr std::size_t kCapacity = 64;

TradeEngine::TradeEngine()
    : queue_(std::make_unique<common::SPSCQueue<int>>(kCapacity)) {}

TradeEngine::~TradeEngine() = default;

void TradeEngine::push() {}