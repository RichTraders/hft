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

#ifndef TRADE_SBE_H
#define TRADE_SBE_H

namespace schema::sbe {

struct SbeTrade {
    int64_t id;              // Trade ID
    double price;            // Decoded from mantissa64 + priceExponent
    double qty;              // Decoded from mantissa64 + qtyExponent
    bool is_buyer_maker;     // boolEnum
    bool is_best_match;      // boolEnum (constant = True in schema)
};

struct SbeTradeEvent {
    int64_t event_time;      // utcTimestampUs
    int64_t transact_time;   // utcTimestampUs
    std::string symbol;      // varString8
    std::vector<SbeTrade> trades;  // Repeating group
};

}  // namespace schema::sbe

#endif  // TRADE_SBE_H
