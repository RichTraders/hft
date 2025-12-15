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

#ifndef MESSAGE_ADAPTER_POLICY_H
#define MESSAGE_ADAPTER_POLICY_H

namespace trading {

struct PointerMessagePolicy {
  template <typename WireMessage>
  static WireMessage adapt(WireMessage msg) {
    return msg;
  }

  template <typename TargetType, typename SourceMessage>
  static TargetType extract(SourceMessage msg) {
    return reinterpret_cast<TargetType>(msg);
  }
};

struct VariantMessagePolicy {
  template <typename WireMessage>
  static const WireMessage& adapt(const WireMessage& msg) {
    return msg;
  }

  template <typename TargetType, typename SourceMessage>
  static TargetType extract(const SourceMessage& msg) {
    return std::get<TargetType>(msg);
  }
};

template <typename AppType>
struct MessagePolicySelector {
  using type = VariantMessagePolicy;
};

}  // namespace trading

#endif  // MESSAGE_ADAPTER_POLICY_H
