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

#ifndef OE_ID_CONSTANTS_H
#define OE_ID_CONSTANTS_H

namespace core::oe_id {
constexpr char kLogin = 'l';
constexpr char kSubscribe = 's';
constexpr char kUnsubscribe = 'u';
constexpr char kOrderPlace = 'p';
constexpr char kOrderCancel = 'c';
constexpr char kOrderReplace = 'r';
constexpr char kOrderModify = 'm';
constexpr char kPing = 'g';
}  // namespace core::oe_id

#endif  // OE_ID_CONSTANTS_H
