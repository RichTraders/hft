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

#ifndef BEST_BID_ASK_SBE_H
#define BEST_BID_ASK_SBE_H

namespace schema::sbe {

struct SbeBestBidAsk {
    int64_t event_time;      // utcTimestampUs - Event generation time
    int64_t book_update_id;  // updateId - Order book update sequence number
    std::string symbol;      // varString8 - Trading pair symbol
    double bid_price;        // Decoded from mantissa64 + priceExponent
    double bid_qty;          // Decoded from mantissa64 + qtyExponent
    double ask_price;        // Decoded from mantissa64 + priceExponent
    double ask_qty;          // Decoded from mantissa64 + qtyExponent
};

}  // namespace schema::sbe

#endif  // BEST_BID_ASK_SBE_H
