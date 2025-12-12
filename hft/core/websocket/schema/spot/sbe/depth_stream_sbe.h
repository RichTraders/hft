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

#ifndef DEPTH_STREAM_SBE_H
#define DEPTH_STREAM_SBE_H

namespace schema::sbe {

struct SbeDepthResponse {
    int64_t event_time;            // utcTimestampUs - Event generation time
    int64_t first_book_update_id;  // updateId - First update ID in this diff
    int64_t last_book_update_id;   // updateId - Last update ID in this diff
    std::string symbol;            // varString8 - Trading pair symbol

    // Repeating groups with groupSize16Encoding
    // Each entry is [price, qty] decoded from mantissa64 + exponent
    // qty = 0 means remove this price level
    std::vector<std::array<double, 2>> bids;  // Bid updates
    std::vector<std::array<double, 2>> asks;  // Ask updates
};

}  // namespace schema::sbe

#endif  // DEPTH_STREAM_SBE_H
