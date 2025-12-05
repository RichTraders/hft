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

#ifndef WS_MD_SBE_DECODER_IMPL_H
#define WS_MD_SBE_DECODER_IMPL_H

namespace schema::sbe {
struct SBEMessageHeader {
    uint16_t block_length;
    uint16_t template_id;
    uint16_t schema_id;
    uint16_t version;
};

struct GroupSize16 {
    uint16_t block_length;
    uint16_t num_in_group;
};

struct GroupSize32 {
    uint16_t block_length;
    uint32_t num_in_group;
};

constexpr size_t kHeaderSize = sizeof(SBEMessageHeader);

inline double decode_mantissa(int64_t mantissa, int8_t exponent) {
    return static_cast<double>(mantissa) * std::pow(10.0, exponent);
}

inline const char* parse_group_header(const char* pos, GroupSize16& size) {
    std::memcpy(&size.block_length, pos, sizeof(uint16_t));
    pos += sizeof(uint16_t);
    std::memcpy(&size.num_in_group, pos, sizeof(uint16_t));
    pos += sizeof(uint16_t);
    return pos;
}

inline const char* parse_group_header(const char* pos, GroupSize32& size) {
    std::memcpy(&size.block_length, pos, sizeof(uint16_t));
    pos += sizeof(uint16_t);
    std::memcpy(&size.num_in_group, pos, sizeof(uint32_t));
    pos += sizeof(uint32_t);
    return pos;
}

inline const char* parse_varString8(const char* pos, std::string& str) {
    uint8_t length;
    std::memcpy(&length, pos, sizeof(uint8_t));
    pos += sizeof(uint8_t);

    if (length > 0) {
        str.assign(pos, length);
        pos += length;
    } else {
        str.clear();
    }

    return pos;
}

inline std::array<double, 2> decode_price_level(
    const char*& pos, int8_t price_exponent, int8_t qty_exponent) {

    int64_t price_mantissa;
    int64_t qty_mantissa;

    std::memcpy(&price_mantissa, pos, sizeof(int64_t));
    pos += sizeof(int64_t);
    std::memcpy(&qty_mantissa, pos, sizeof(int64_t));
    pos += sizeof(int64_t);

    const double price = decode_mantissa(price_mantissa, price_exponent);
    const double qty = decode_mantissa(qty_mantissa, qty_exponent);

    return {price, qty};
}

}  // namespace schema::sbe

#endif  // WS_MD_SBE_DECODER_IMPL_H
