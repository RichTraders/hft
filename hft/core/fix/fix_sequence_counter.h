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

#ifndef FIX_SEQUENCE_COUNTER_H
#define FIX_SEQUENCE_COUNTER_H
namespace core {
class FixSequenceCounter {
 public:
  bool is_valid(const std::string& message);

 private:
  static constexpr size_t kIdxGap = 3;
  static constexpr std::string kMessageSequenceStartKeyword = "34=";
  uint64_t current_sequence_ = 0;
};
}  // namespace core

#endif  //FIX_SEQUENCE_COUNTER_H
