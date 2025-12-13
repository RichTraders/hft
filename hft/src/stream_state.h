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

#ifndef STREAM_STATE_H
#define STREAM_STATE_H

namespace trading {

enum class StreamState : uint8_t {
  kRunning,
  kAwaitingSnapshot,
  kApplyingSnapshot,
  kBuffering,
};

}  // namespace trading

#endif  // STREAM_STATE_H
