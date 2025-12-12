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

#ifndef SNAPSHOT_SBE_H
#define SNAPSHOT_SBE_H

namespace schema::sbe {

struct SbeDepthSnapshot {
    int64_t event_time;      // utcTimestampUs - Event generation time
    int64_t book_update_id;  // updateId - Order book update sequence number
    std::string symbol;      // varString8 - Trading pair symbol

    std::vector<std::array<double, 2>> bids;  // Bids sorted by price descending
    std::vector<std::array<double, 2>> asks;  // Asks sorted by price ascending
};

}  // namespace schema::sbe

#endif  // SNAPSHOT_SBE_H
