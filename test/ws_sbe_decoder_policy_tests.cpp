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

#include <gtest/gtest.h>
#include "common/logger.h"
#include "core/websocket/market_data/decoder_policy.h"
#include <fstream>

namespace {

std::vector<char> load_binary_data(const std::string& filename) {
  std::string full_path = "data/sbe/" + filename;
  std::ifstream file(full_path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Cannot open test data file: " + full_path);
  }
  return {std::istreambuf_iterator<char>(file),
      std::istreambuf_iterator<char>()};
}

template <typename T, typename VariantT>
bool holds_type(const VariantT& var) {
  return std::holds_alternative<T>(var);
}

}  // namespace

class SbeDecoderPolicyTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    logger_ = std::make_unique<common::Logger>();
    logger_->setLevel(common::LogLevel::kDebug);
    logger_->clearSink();
    producer_ =
        std::make_unique<common::Logger::Producer>(logger_->make_producer());
  }

  static void TearDownTestSuite() {
    producer_.reset();
    logger_->shutdown();
    logger_.reset();
  }

  core::SbeDecoderPolicy decoder_;
  static std::unique_ptr<common::Logger> logger_;
  static std::unique_ptr<common::Logger::Producer> producer_;
};

std::unique_ptr<common::Logger> SbeDecoderPolicyTest::logger_;
std::unique_ptr<common::Logger::Producer> SbeDecoderPolicyTest::producer_;

TEST_F(SbeDecoderPolicyTest, DecodeTradeEventFromBinFile) {
  const auto binary_data = load_binary_data("trade.bin");
  ASSERT_FALSE(binary_data.empty());

  auto wire_msg =
      decoder_.decode({binary_data.data(), binary_data.size()}, *producer_);

  ASSERT_TRUE(holds_type<schema::sbe::SbeTradeEvent>(wire_msg))
      << "Expected SbeTradeEvent variant type";

  const auto& trade_event = std::get<schema::sbe::SbeTradeEvent>(wire_msg);

  // Verify content
  EXPECT_EQ(trade_event.symbol, "BTCUSDT");
  ASSERT_FALSE(trade_event.trades.empty());

  const auto& first_trade = trade_event.trades[0];
  EXPECT_EQ(first_trade.id, 5606933548);
  EXPECT_DOUBLE_EQ(first_trade.price, 93166.56);
  EXPECT_DOUBLE_EQ(first_trade.qty, 0.00039);
}

TEST_F(SbeDecoderPolicyTest, DecodeBestBidAskFromBinFile) {
    const auto binary_data = load_binary_data("bbo.bin");
    ASSERT_FALSE(binary_data.empty());

    auto wire_msg = decoder_.decode({binary_data.data(), binary_data.size()}, *producer_);

    ASSERT_TRUE(holds_type<schema::sbe::SbeBestBidAsk>(wire_msg)) << "Expected SbeBestBidAsk variant type";

    const auto& event = std::get<schema::sbe::SbeBestBidAsk>(wire_msg);

    // Verify content
    EXPECT_EQ(event.symbol, "BTCUSDT");
    EXPECT_DOUBLE_EQ(event.bid_price, 93263.26);
    EXPECT_DOUBLE_EQ(event.ask_price, 93263.27);
}

TEST_F(SbeDecoderPolicyTest, DecodeDepthSnapshotFromBinFile) {
    const auto binary_data = load_binary_data("snapshot.bin");
    ASSERT_FALSE(binary_data.empty());

    auto wire_msg = decoder_.decode({binary_data.data(), binary_data.size()}, *producer_);

    ASSERT_TRUE(holds_type<schema::sbe::SbeDepthSnapshot>(wire_msg)) << "Expected SbeDepthSnapshot variant type";

    const auto& event = std::get<schema::sbe::SbeDepthSnapshot>(wire_msg);

    // Verify content
    EXPECT_EQ(event.symbol, "BTCUSDT");
    ASSERT_FALSE(event.bids.empty());
    ASSERT_FALSE(event.asks.empty());
    EXPECT_DOUBLE_EQ(event.bids[0][0], 93263.26);
    EXPECT_DOUBLE_EQ(event.bids[0][1], 0.87238);
    EXPECT_DOUBLE_EQ(event.asks[0][0], 93263.27);
    EXPECT_DOUBLE_EQ(event.asks[0][1], 3.25577);
}

std::string convertEpochToCustomFormat(long long epoch_us) {
  long long seconds = epoch_us / 1000000;
  long long microseconds = epoch_us % 1000000;

  time_t tt = static_cast<time_t>(seconds);

  tm* local_tm = localtime(&tt);

  std::stringstream ss;

  ss << std::put_time(local_tm, "%Y%m%d-%H:%M:%S");

  ss << "." << std::setw(6) << std::setfill('0') << microseconds;

  return ss.str();
}

std::string doubleToString(double value, int precision) {
  std::stringstream ss;

  ss << std::fixed;
  ss << std::setprecision(precision);
  ss << value;
  return ss.str();
}

TEST_F(SbeDecoderPolicyTest, DecodeDepthDiffFromBinFile) {
    const auto binary_data = load_binary_data("market_data.bin");
    ASSERT_FALSE(binary_data.empty());

    auto wire_msg = decoder_.decode({binary_data.data(), binary_data.size()}, *producer_);

    ASSERT_TRUE(holds_type<schema::sbe::SbeDepthResponse>(wire_msg)) << "Expected SbeDepthResponse variant type";

    const auto& event = std::get<schema::sbe::SbeDepthResponse>(wire_msg);

    // Verify content with actual values
    EXPECT_EQ(event.symbol, "BTCUSDT");
    ASSERT_FALSE(event.bids.empty());
    EXPECT_DOUBLE_EQ(event.bids[0][0], 93165.14);
    EXPECT_DOUBLE_EQ(event.bids[0][1], 6.72569);
}

TEST_F(SbeDecoderPolicyTest, MakeFixData) {
    // const auto binary_data = load_binary_data("market_data.bin");
    // ASSERT_FALSE(binary_data.empty());
    //
    // auto wire_msg = decoder_.decode({binary_data.data(), binary_data.size()}, *producer_);
    //
    // ASSERT_TRUE(holds_type<schema::sbe::SbeDepthResponse>(wire_msg))
    //     << "Expected SbeDepthResponse variant type";
    //
    // const auto& event = std::get<schema::sbe::SbeDepthResponse>(wire_msg);

    // // Generate FIX message
    // const auto SOH = '\x01';
    // std::stringstream body;  // Body 먼저 생성 (길이 계산 위해)
    //
    // // Body 내용 작성
    // body << "35=X" << SOH;
    // body << "49=SPOT" << SOH;
    // body << "56=BMDWATCH" << SOH;
    // body << "34=1" << SOH;  // MsgSeqNum (간단히 1로)
    // body << "52=" << convertEpochToCustomFormat(event.event_time) << SOH;
    // body << "262=DEPTH_STREAM" << SOH;
    //
    // // NoMDEntries 개수 = bids + asks
    // const size_t total_entries = event.bids.size() + event.asks.size();
    // body << "268=" << total_entries << SOH;
    //
    // // Update IDs를 문자열로 변환
    // std::string first_update_id = std::to_string(event.first_book_update_id);
    // std::string last_update_id = std::to_string(event.last_book_update_id);
    //
    // // Bid entries
    // for (const auto& bid : event.bids) {
    //     body << "279=1" << SOH;  // MDUpdateAction: 1=CHANGE
    //     body << "269=0" << SOH;  // MDEntryType: 0=BID
    //     body << "270=" << doubleToString(bid[0], 8) << SOH;  // MDEntryPx
    //     body << "271=" << doubleToString(bid[1], 8) << SOH;  // MDEntrySize
    //     body << "55=" << event.symbol << SOH;  // Symbol
    //     body << "25043=" << first_update_id << SOH;  // FirstBookUpdateID
    //     body << "25044=" << last_update_id << SOH;   // LastBookUpdateID
    // }
    //
    // // Ask entries
    // for (const auto& ask : event.asks) {
    //     body << "279=1" << SOH;  // MDUpdateAction: 1=CHANGE
    //     body << "269=1" << SOH;  // MDEntryType: 1=OFFER (ASK)
    //     body << "270=" << doubleToString(ask[0], 8) << SOH;  // MDEntryPx
    //     body << "271=" << doubleToString(ask[1], 8) << SOH;  // MDEntrySize
    //     body << "55=" << event.symbol << SOH;  // Symbol
    //     body << "25043=" << first_update_id << SOH;  // FirstBookUpdateID
    //     body << "25044=" << last_update_id << SOH;   // LastBookUpdateID
    // }
    //
    // std::string body_str = body.str();
    //
    // // Body Length 계산
    // std::stringstream body_length;
    // body_length << std::setw(7) << std::setfill('0') << body_str.length();
    //
    // // CheckSum 계산 (Header + Body)
    // std::string msg_without_checksum = "8=FIX.4.4" + std::string(1, SOH) +
    //                                    "9=" + body_length.str() + std::string(1, SOH) +
    //                                    body_str;
    //
    // int checksum = 0;
    // for (char c : msg_without_checksum) {
    //     checksum += static_cast<unsigned char>(c);
    // }
    // checksum %= 256;
    //
    // std::stringstream checksum_str;
    // checksum_str << std::setw(3) << std::setfill('0') << checksum;
    //
    // // 최종 FIX 메시지 작성
    // std::ofstream writeFile("./data/fix/market_data.fix", std::ios::binary);
    // if (!writeFile.is_open()) {
    //     // 디렉토리 생성 시도
    //     std::filesystem::create_directories("./data/fix");
    //     writeFile.open("./data/fix/market_data.fix", std::ios::binary);
    //     ASSERT_TRUE(writeFile.is_open()) << "Failed to create FIX file";
    // }
    //
    // writeFile << "8=FIX.4.4" << SOH;
    // writeFile << "9=" << body_length.str() << SOH;
    // writeFile << body_str;
    // writeFile << "10=" << checksum_str.str() << SOH;
    //
    // writeFile.close();
    //
    // std::cout << "\nFIX message written to: ./data/fix/market_data.fix\n";
    // std::cout << "  Total entries: " << total_entries
    //           << " (" << event.bids.size() << " bids + "
    //           << event.asks.size() << " asks)\n";
    // std::cout << "  Body length: " << body_str.length() << " bytes\n";
    // std::cout << "  Checksum: " << checksum_str.str() << "\n";

    // Generate JSON message (compact format, no whitespace)
    // std::stringstream json;
    // json << "{\"e\":\"depthUpdate\",\"E\":" << event.event_time
    //      << ",\"s\":\"" << event.symbol
    //      << "\",\"U\":" << event.first_book_update_id
    //      << ",\"u\":" << event.last_book_update_id << ",\"b\":[";
    //
    // // Bids array
    // for (size_t i = 0; i < event.bids.size(); ++i) {
    //     json << "[\"" << doubleToString(event.bids[i][0], 8) << "\","
    //          << "\"" << doubleToString(event.bids[i][1], 8) << "\"]";
    //     if (i < event.bids.size() - 1) {
    //         json << ",";
    //     }
    // }
    // json << "],\"a\":[";
    //
    // // Asks array
    // for (size_t i = 0; i < event.asks.size(); ++i) {
    //     json << "[\"" << doubleToString(event.asks[i][0], 8) << "\","
    //          << "\"" << doubleToString(event.asks[i][1], 8) << "\"]";
    //     if (i < event.asks.size() - 1) {
    //         json << ",";
    //     }
    // }
    // json << "]}";
    //
    // std::string json_str = json.str();
    //
    // // Write JSON file
    // std::ofstream jsonFile("./data/benchmark/json.txt");
    // if (!jsonFile.is_open()) {
    //     ASSERT_TRUE(jsonFile.is_open()) << "Failed to create JSON file";
    // }
    //
    // jsonFile << json_str;
    // jsonFile.close();
    //
    // std::cout << "\nJSON message written to: ./data/market_data/depth.json\n";
    // std::cout << "  JSON length: " << json_str.length() << " bytes\n";
}