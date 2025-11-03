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

#include "fix_sequence_counter.h"
namespace core {
bool FixSequenceCounter::is_valid(const std::string& message) {
  const auto first_idx = message.find(kMessageSequenceStartKeyword);
  if (first_idx == std::string::npos)
    return false;
  const auto last_idx = message.find('\x01', first_idx);
  const auto next_candidate_sequence =
      std::stoull(message.substr(first_idx + kIdxGap, last_idx));
  const bool ret = next_candidate_sequence == current_sequence_ + 1;
  current_sequence_ = next_candidate_sequence;

  return ret;
}
}  // namespace core