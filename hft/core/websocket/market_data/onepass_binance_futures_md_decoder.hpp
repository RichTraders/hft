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

#ifndef ONEPASS_BINANCE_FUTURES_MD_DECODER_H
#define ONEPASS_BINANCE_FUTURES_MD_DECODER_H

#include <common/logger.h>
#include <common/fixed_point_config.hpp>
#include <glaze/glaze.hpp>

#include "global.h"
#include "schema/futures/response/api_response.h"
#include "schema/futures/response/book_ticker.h"
#include "schema/futures/response/depth_stream.h"
#include "schema/futures/response/exchange_info_response.h"
#include "schema/futures/response/snapshot.h"
#include "schema/futures/response/trade.h"

namespace core {

// Import scale constants from global common namespace before entering nested namespaces
inline constexpr int64_t kGlobalPriceScale =
    ::common::FixedPointConfig::kPriceScale;
inline constexpr int64_t kGlobalQtyScale =
    ::common::FixedPointConfig::kQtyScale;

namespace onepass {

// Fixed offsets for JSON parsing (compact JSON, no whitespace)
namespace offset {
// Common offsets - calculated from actual JSON patterns
constexpr size_t kStreamValueStart = sizeof(R"({"stream":")") - 1;    // 11
constexpr size_t kDataEventStart = sizeof(R"(","data":{"e":")") - 1;  // 15
constexpr size_t kPriceQtyEntry = sizeof(R"([")") - 1;                // 2

// Offset patterns after quoted string end
constexpr size_t kSkipQuoteKeyValue =
    sizeof(R"(","X":)") - 1;  // 6: key with unquoted value
constexpr size_t kSkipKeyValue =
    sizeof(R"(,"X":)") - 1;  // 5: key with unquoted value
constexpr size_t kSkipQuoteKeyQuote =
    sizeof(R"(,"X":")") - 1;  // 6: key with quoted value

// Depth specific
constexpr size_t kDepthPuSkip = sizeof(R"(,"pu":)") - 1;      // 6
constexpr size_t kDepthBidsStart = sizeof(R"(,"b":[)") - 1;   // 6
constexpr size_t kDepthAsksStart = sizeof(R"(],"a":[)") - 1;  // 7

// Trade specific
constexpr size_t kTradeAggIdSkip = sizeof(R"(,"a":)") - 1;    // 5
constexpr size_t kTradePriceSkip = sizeof(R"(","p":")") - 1;  // 7
constexpr size_t kTradeQtySkip = sizeof(R"(,"q":")") - 1;     // 6
constexpr size_t kTradeFirstIdSkip = sizeof(R"(,"f":)") - 1;  // 5

// BookTicker specific
constexpr size_t kBookTickerSkipET =
    sizeof(R"(,"E":)") - 1;  // 5: skip ,"E": or ,"T":
constexpr size_t kBookTickerSymbolSkip =
    sizeof(R"(s":")") - 1;  // 4: after finding 's'
constexpr size_t kBookTickerBidPriceSkip = sizeof(R"(","b":")") - 1;  // 7
constexpr size_t kBookTickerBidQtySkip = sizeof(R"(,"B":")") - 1;     // 6
constexpr size_t kBookTickerAskPriceSkip = sizeof(R"(,"a":")") - 1;   // 6
constexpr size_t kBookTickerAskQtySkip = sizeof(R"(,"A":")") - 1;     // 6

// Snapshot specific
constexpr size_t kSnapshotIdStart = sizeof(R"({"id":")") - 1;         // 7
constexpr size_t kSnapshotStatusSkip = sizeof(R"(","status":)") - 1;  // 11
constexpr size_t kSnapshotResultSkip =
    sizeof(R"(,"result":{"lastUpdateId":)") - 1;                    // 26
constexpr size_t kSnapshotBidsStart = sizeof(R"(,"bids":[)") - 1;   // 9
constexpr size_t kSnapshotAsksStart = sizeof(R"(],"asks":[)") - 1;  // 10

// Reserve sizes
constexpr size_t kDepthReserve = 300;
constexpr size_t kSnapshotReserve = 1000;

// Search limits for memchr
constexpr size_t kMaxStringSearchLen = 64;

// Minimum valid JSON payload
constexpr size_t kMinPayloadLen = sizeof(R"({"s":""})") - 1;  // 8

// Dispatch constants
constexpr size_t kFirstCharOffset = 2;  // position of first key char in {"X
constexpr size_t kAtSearchStart =
    sizeof(R"({"stream":"x)") - 1;       // 12: minimum before @ in stream
constexpr size_t kStreamTypeOffset = 1;  // +1 after @ to get stream type char

// Digit parsing constants
constexpr uint8_t kDigitBase = '0';
constexpr uint8_t kDigitRange = 10;
constexpr double kDecimalBase = 10.0;
constexpr size_t kShortSearchLen = 32;
}  // namespace offset

namespace ofs = offset;

// Check if character is a digit
[[gnu::always_inline]] inline bool is_digit(char chr) noexcept {
  return static_cast<uint8_t>(chr - ofs::kDigitBase) < ofs::kDigitRange;
}

// NOLINTNEXTLINE(modernize-avoid-c-arrays)
static constexpr int64_t kPowersOf10[] = {1LL,
    10LL,
    100LL,
    1000LL,
    10000LL,
    100000LL,
    1000000LL,
    10000000LL,
    100000000LL,
    1000000000LL,
    10000000000LL};

// NOLINTNEXTLINE(modernize-avoid-c-arrays)
static constexpr double kDoublePowersOf10[] =
    {1.0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10};

[[gnu::always_inline]] inline double parse_double_inline(
    const char*& ptr) noexcept {
  uint64_t mantissa = 0;
  int frac_digits = 0;
  bool in_frac = false;

  // NOLINTBEGIN(readability-identifier-length,bugprone-narrowing-conversions)
  while (true) {
    const char chr = *ptr;
    if (is_digit(chr)) {
      mantissa = mantissa * ofs::kDecimalBase +
                 static_cast<uint64_t>(chr - ofs::kDigitBase);
      if (in_frac)
        ++frac_digits;
      ++ptr;
    } else if (chr == '.') {
      in_frac = true;
      ++ptr;
    } else {
      break;
    }
  }
  // NOLINTEND(readability-identifier-length,bugprone-narrowing-conversions)
  ++ptr;  // skip closing '"'

  if (frac_digits == 0) {
    return static_cast<double>(mantissa);
  }
  return static_cast<double>(mantissa) / kDoublePowersOf10[frac_digits];
}

template <int64_t Scale>
[[gnu::always_inline]] inline int64_t parse_fixed_inline(
    const char*& ptr) noexcept {
  int64_t mantissa = 0;
  int frac_digits = 0;
  bool in_frac = false;

  // NOLINTBEGIN(readability-identifier-length,bugprone-narrowing-conversions)
  while (true) {
    const char chr = *ptr;
    if (is_digit(chr)) {
      mantissa = mantissa * ofs::kDecimalBase +
                 static_cast<int64_t>(chr - ofs::kDigitBase);
      if (in_frac)
        ++frac_digits;
      ++ptr;
    } else if (chr == '.') {
      in_frac = true;
      ++ptr;
    } else {
      break;
    }
  }
  // NOLINTEND(readability-identifier-length,bugprone-narrowing-conversions)
  ++ptr;  // skip closing '"'

  // ex: "98234.12" with Scale=10000
  //     mantissa=9823412, frac_digits=2
  //     result = 9823412 * (10000 / 100) = 982341200
  if (frac_digits == 0) {
    return mantissa * Scale;
  }

  const int64_t scale_divisor = kPowersOf10[frac_digits];
  return mantissa * (Scale / scale_divisor);
}

[[gnu::always_inline]] inline void skip_digits(const char*& ptr) noexcept {
  while (is_digit(*ptr)) {
    ++ptr;
  }
}

[[gnu::always_inline]] inline const char* skip_to(const char* ptr,
    char chr) noexcept {
  const char* found =
      static_cast<const char*>(std::memchr(ptr, chr, ofs::kShortSearchLen));
  return found ? found : ptr;
}

// Parse unsigned integer inline
[[gnu::always_inline]] inline uint64_t parse_uint_inline(
    const char*& ptr) noexcept {
  uint64_t val = 0;
  while (is_digit(*ptr)) {
    val = val * ofs::kDigitRange + (*ptr++ - ofs::kDigitBase);
  }
  return val;
}

}  // namespace onepass

namespace ofs = onepass::offset;

class OnepassBinanceFuturesMdDecoder {
 public:
  using DepthResponse = schema::futures::DepthResponse;
  using TradeEvent = schema::futures::TradeEvent;
  using BookTickerEvent = schema::futures::BookTickerEvent;
  using DepthSnapshot = schema::futures::DepthSnapshot;
  using ApiResponse = schema::futures::ApiResponse;
  using ExchangeInfoResponse = schema::futures::ExchangeInfoHttpResponse;

  using WireMessage = std::variant<std::monostate, DepthResponse, TradeEvent,
      BookTickerEvent, DepthSnapshot, ApiResponse, ExchangeInfoResponse>;

  static constexpr std::string_view protocol_name() { return "json"; }
  static constexpr bool requires_api_key() { return false; }

  explicit OnepassBinanceFuturesMdDecoder(
      const common::Logger::Producer& logger)
      : logger_(logger) {}

  [[nodiscard]] WireMessage decode(std::string_view payload) const {
    if (payload.size() < ofs::kMinPayloadLen) [[unlikely]] {
      return WireMessage{};
    }

    // Fast dispatch based on payload prefix patterns
    // snapshot: {"id":"snapshot_BTCUSDT...
    // depth:    {"stream":"btcusdt@depth"...
    // trade:    {"stream":"btcusdt@aggTrade"...
    // exchange: {"timezone":"UTC","serverTime...
    // bookTicker: {"stream":"btcusdt@bookTicker"...

    const char first_chr = payload[ofs::kFirstCharOffset];

    if (first_chr == 's') [[likely]] {
      const char* p_start = payload.data() + ofs::kAtSearchStart;
      const char* at_ptr = static_cast<const char*>(
          std::memchr(p_start, '@', payload.size() - ofs::kAtSearchStart));
      if (at_ptr) [[likely]] {
        const char stream_type = *(at_ptr + ofs::kStreamTypeOffset);
        if (stream_type == 'd')
          return decode_depth(payload);
        if (stream_type == 'a')
          return decode_trade(payload);
        if (stream_type == 'b')
          return decode_book_ticker(payload);
      }
    } else if (first_chr == 'i') {
      return decode_snapshot(payload);
    } else if (first_chr == 't') {
      return decode_with_glaze<ExchangeInfoResponse, "[ExchangeInfo]">(payload);
    }

    if (auto api_response = glz::read_json<ApiResponse>(payload)) {
      return WireMessage{std::in_place_type<ApiResponse>,
          std::move(*api_response)};
    }

    return WireMessage{};
  }

 private:
  // Format: {"stream":"btcusdt@depth","data":{"e":"depthUpdate","E":...,"T":...,"s":"BTCUSDT","U":...,"u":...,"pu":...,"b":[...],"a":[...]}}
  // Used fields: symbol, start_update_id, end_update_id, final_update_id_in_last_stream, bids, asks
  [[nodiscard]] WireMessage decode_depth(std::string_view payload) const {
    DepthResponse result;  // NOLINT(misc-const-correctness)
    const char* ptr = payload.data();

    // Skip: stream, event_type, E, T (unused)
    ptr += ofs::kStreamValueStart;
    ptr = onepass::skip_to(ptr, '"');  // stream_end

    ptr = ptr + ofs::kDataEventStart;
    ptr = onepass::skip_to(ptr, '"');  // e_end

    ptr = ptr + ofs::kSkipQuoteKeyValue;  // ","E":
    onepass::skip_digits(ptr);

    ptr += ofs::kSkipKeyValue;  // ,"T":
    onepass::skip_digits(ptr);

    ptr += ofs::kSkipQuoteKeyQuote;  // ,"s":"
    const char* s_end = onepass::skip_to(ptr, '"');
    result.data.symbol.assign(ptr, s_end);

    ptr = s_end + ofs::kSkipQuoteKeyValue;  // ","U":
    result.data.start_update_id = onepass::parse_uint_inline(ptr);

    ptr += ofs::kSkipKeyValue;  // ,"u":
    result.data.end_update_id = onepass::parse_uint_inline(ptr);

    ptr += ofs::kDepthPuSkip;  // ,"pu":
    result.data.final_update_id_in_last_stream =
        onepass::parse_uint_inline(ptr);

    ptr += ofs::kDepthBidsStart;  // ,"b":[
    result.data.bids.reserve(ofs::kDepthReserve);
    while (*ptr == '[') {
      ptr += ofs::kPriceQtyEntry;
      const int64_t price = onepass::parse_fixed_inline<kGlobalPriceScale>(ptr);
      ptr += ofs::kPriceQtyEntry;
      const int64_t qty = onepass::parse_fixed_inline<kGlobalQtyScale>(ptr);
      ++ptr;
      result.data.bids.push_back({price, qty});
      if (*ptr == ',')
        ++ptr;
    }

    ptr += ofs::kDepthAsksStart;  // ],"a":[
    result.data.asks.reserve(ofs::kDepthReserve);
    while (*ptr == '[') {
      ptr += ofs::kPriceQtyEntry;
      const int64_t price = onepass::parse_fixed_inline<kGlobalPriceScale>(ptr);
      ptr += ofs::kPriceQtyEntry;
      const int64_t qty = onepass::parse_fixed_inline<kGlobalQtyScale>(ptr);
      ++ptr;
      result.data.asks.push_back({price, qty});
      if (*ptr == ',')
        ++ptr;
    }

    return WireMessage{std::in_place_type<DepthResponse>, std::move(result)};
  }

  // Format: {"stream":"btcusdt@aggTrade","data":{"e":"aggTrade","E":...,"a":...,"s":"BTCUSDT","ptr":"...","q":"...","f":...,"l":...,"T":...,"m":...}}
  // Used fields: symbol, price, quantity, is_buyer_market_maker
  [[nodiscard]] WireMessage decode_trade(std::string_view payload) const {
    TradeEvent result;  // NOLINT(misc-const-correctness)
    const char* ptr = payload.data();

    // Skip: stream, event_type, E, a (unused)
    ptr += ofs::kStreamValueStart;
    ptr = onepass::skip_to(ptr, '"');  // stream_end

    ptr = ptr + ofs::kDataEventStart;
    ptr = onepass::skip_to(ptr, '"');  // e_end

    ptr = ptr + ofs::kSkipQuoteKeyValue;  // ","E":
    onepass::skip_digits(ptr);

    ptr += ofs::kTradeAggIdSkip;  // ,"a":
    onepass::skip_digits(ptr);

    ptr += ofs::kSkipQuoteKeyQuote;  // ,"s":"
    const char* s_end = onepass::skip_to(ptr, '"');
    result.data.symbol.assign(ptr, s_end);

    ptr = s_end + ofs::kTradePriceSkip;  // ","ptr":"
    result.data.price = onepass::parse_fixed_inline<kGlobalPriceScale>(ptr);

    ptr += ofs::kTradeQtySkip;  // ","q":"
    result.data.quantity = onepass::parse_fixed_inline<kGlobalQtyScale>(ptr);

    ptr += ofs::kTradeFirstIdSkip;  // ","f":
    onepass::skip_digits(ptr);

    ptr += ofs::kSkipKeyValue;  // ,"l":
    onepass::skip_digits(ptr);

    ptr += ofs::kSkipKeyValue;  // ,"T":
    onepass::skip_digits(ptr);

    ptr += ofs::kSkipKeyValue;  // ,"m":
    result.data.is_buyer_market_maker = (*ptr == 't');

    return WireMessage{std::in_place_type<TradeEvent>, std::move(result)};
  }

  // Format: {"stream":"xrpusdc@bookTicker","data":{"e":"bookTicker","u":...,"s":"XRPUSDC","b":"...","B":"...","a":"...","A":"...","T":...,"E":...}}
  // Used fields: symbol, update_id, best_bid_price, best_bid_qty, best_ask_price, best_ask_qty
  [[nodiscard]] WireMessage decode_book_ticker(std::string_view payload) const {
    BookTickerEvent result;  // NOLINT(misc-const-correctness)
    const char* ptr = payload.data();

    ptr += ofs::kStreamValueStart;
    ptr = onepass::skip_to(ptr, '"');  // stream_end

    ptr = ptr + ofs::kDataEventStart;
    ptr = onepass::skip_to(ptr, '"');  // e_end

    ptr = ptr + ofs::kSkipQuoteKeyValue;  // ","u":
    result.data.update_id = onepass::parse_uint_inline(ptr);

    ptr += ofs::kSkipQuoteKeyQuote;  // ,"s":"
    const char* s_end = onepass::skip_to(ptr, '"');
    result.data.symbol.assign(ptr, s_end);

    ptr = s_end + ofs::kBookTickerBidPriceSkip;  // ","b":"
    result.data.best_bid_price =
        onepass::parse_fixed_inline<kGlobalPriceScale>(ptr);

    ptr += ofs::kBookTickerBidQtySkip;  // ,"B":"
    result.data.best_bid_qty =
        onepass::parse_fixed_inline<kGlobalQtyScale>(ptr);

    ptr += ofs::kBookTickerAskPriceSkip;  // ,"a":"
    result.data.best_ask_price =
        onepass::parse_fixed_inline<kGlobalPriceScale>(ptr);

    ptr += ofs::kBookTickerAskQtySkip;  // ,"A":"
    result.data.best_ask_qty =
        onepass::parse_fixed_inline<kGlobalQtyScale>(ptr);

    return WireMessage{std::in_place_type<BookTickerEvent>, std::move(result)};
  }

  // Format: {"id":"snapshot_BTCUSDT","status":200,"result":{"lastUpdateId":...,"E":...,"T":...,"bids":[...],"asks":[...]}}
  // Used fields: id (for symbol extraction), book_update_id, bids, asks
  [[nodiscard]] WireMessage decode_snapshot(std::string_view view) const {
    DepthSnapshot result;  // NOLINT(misc-const-correctness)
    const char* ptr = view.data();

    ptr += ofs::kSnapshotIdStart;  // {"id":"
    const char* id_end = onepass::skip_to(ptr, '"');
    result.id.assign(ptr, id_end);

    ptr = id_end + ofs::kSnapshotStatusSkip;  // ","status":
    onepass::skip_digits(ptr);

    ptr += ofs::kSnapshotResultSkip;  // ,"result":{"lastUpdateId":
    result.result.book_update_id = onepass::parse_uint_inline(ptr);

    ptr += ofs::kSkipKeyValue;  // ,"E":
    onepass::skip_digits(ptr);

    ptr += ofs::kSkipKeyValue;  // ,"T":
    onepass::skip_digits(ptr);

    ptr += ofs::kSnapshotBidsStart;  // ,"bids":[
    result.result.bids.reserve(ofs::kSnapshotReserve);
    while (*ptr == '[') {
      ptr += ofs::kPriceQtyEntry;
      const int64_t price = onepass::parse_fixed_inline<kGlobalPriceScale>(ptr);
      ptr += ofs::kPriceQtyEntry;
      const int64_t qty = onepass::parse_fixed_inline<kGlobalQtyScale>(ptr);
      ++ptr;
      result.result.bids.push_back({price, qty});
      if (*ptr == ',')
        ++ptr;
    }

    ptr += ofs::kSnapshotAsksStart;  // ],"asks":[
    result.result.asks.reserve(ofs::kSnapshotReserve);
    while (*ptr == '[') {
      ptr += ofs::kPriceQtyEntry;
      const int64_t price = onepass::parse_fixed_inline<kGlobalPriceScale>(ptr);
      ptr += ofs::kPriceQtyEntry;
      const int64_t qty = onepass::parse_fixed_inline<kGlobalQtyScale>(ptr);
      ++ptr;
      result.result.asks.push_back({price, qty});
      if (*ptr == ',')
        ++ptr;
    }

    return WireMessage{std::in_place_type<DepthSnapshot>, std::move(result)};
  }

  template <class T, FixedString Label>
  [[nodiscard]] WireMessage decode_with_glaze(std::string_view payload) const {
    auto parsed = glz::read_json<T>(payload);
    if (!parsed) {
      auto error_msg = glz::format_error(parsed.error(), payload);
      LOG_ERROR(logger_,
          "\x1b[31m Failed to decode {} response: "
          "{}. payload:{} \x1b[0m",
          Label.view(),
          error_msg,
          payload);
      return WireMessage{};
    }
    return WireMessage{std::in_place_type<T>, std::move(*parsed)};
  }

  const common::Logger::Producer& logger_;
};

}  // namespace core

#endif  // ONEPASS_BINANCE_FUTURES_MD_DECODER_H
