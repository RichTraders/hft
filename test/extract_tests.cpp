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
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace core {

static bool strip_to_header(std::string& buffer);

static bool peek_full_message_len(const std::string& buffer, size_t& msg_len);

static bool extract_next_message(std::string& buffer, std::string& msg);

bool strip_to_header(std::string& buffer) {
  const std::string kFixSignature = "8=FIX";
  const size_t pos = buffer.find(kFixSignature);
  if (pos == std::string::npos) {
    if (buffer.size() > kFixSignature.size() - 1) {
      buffer.erase(0, buffer.size() - (kFixSignature.size() - 1));
    }
    return false;
  }
  if (pos > 0)
    buffer.erase(0, pos);
  return true;
}

bool peek_full_message_len(const std::string& buffer, size_t& msg_len) {
  const size_t body_start = buffer.find("9=");
  if (body_start == std::string::npos)
    return false;

  const size_t body_end = buffer.find('\x01', body_start);
  if (body_end == std::string::npos)
    return false;

  const int body_len =
      std::stoi(buffer.substr(body_start + 2, body_end - (body_start + 2)));
  const size_t header_len = (body_end + 1);
  msg_len = header_len + body_len +
            7;  // NOLINT(readability-magic-numbers) 7 = "10=" + 3bytes + SOH
  return buffer.size() >= msg_len;
}

bool extract_next_message(std::string& buffer, std::string& msg) {
  if (!strip_to_header(buffer))
    return false;

  size_t msg_len = 0;
  if (!peek_full_message_len(buffer, msg_len))
    return false;

  msg = buffer.substr(0, msg_len);
  buffer.erase(0, msg_len);
  return true;
}
// 간단한 헬퍼: 가장 작은 valid FIX(Heartbeat) 메시지 생성
// body: "35=0<SOH>" -> 길이 5
// 전체: "8=FIX.4.2<SOH>9=5<SOH>35=0<SOH>10=000<SOH>"
static std::string MakeMinimalFix() {
  const std::string SOH = "\x01";
  return "8=FIX.4.2" + SOH + "9=5" + SOH + "35=0" + SOH + "10=000" + SOH;
}

TEST(StripToHeaderTest, ClearsOnNoHeader) {
  std::string buf = "garbage_without_header";
  EXPECT_FALSE(strip_to_header(buf));
  EXPECT_FALSE(buf.empty());
  EXPECT_NE(buf, "garbage_without_header");
}

TEST(StripToHeaderTest, ErasesGarbageBeforeHeader) {
  const std::string SOH = "\x01";
  std::string valid =
      "8=FIX.4.4" + SOH + "9=5" + SOH + "35=0" + SOH + "10=000" + SOH;

  std::string buf = "noise_noise" + valid;
  EXPECT_TRUE(strip_to_header(buf));
  EXPECT_EQ(buf, valid);
}

TEST(PeekFullLenTest, ReturnsFalseIfNoBodyLengthTag) {
  const std::string SOH = "\x01";
  std::string buf =
      "8=FIX.4.2" + SOH + "35=0" + SOH + "10=000" + SOH;  // 9= 없음
  size_t len = 0;
  EXPECT_FALSE(peek_full_message_len(buf, len));
}

TEST(PeekFullLenTest, ReturnsFalseIfNoSohAfterBodyLen) {
  // "9=5" 뒤에 SOH가 없음
  std::string buf =
      "8=FIX.4.2\x019=5"
      "35=0\x01"
      "10=000\x01";
  size_t len = 0;
  EXPECT_FALSE(peek_full_message_len(buf, len));
}

TEST(PeekFullLenTest, ReturnsFalseIfBufferSmallerThanComputedLength) {
  const std::string SOH = "\x01";
  // body_len=10 이라고 주장하지만 실제로는 본문이 부족함
  std::string buf =
      "8=FIX.4.2" + SOH + "9=10" + SOH + "35=0" + SOH + "10=000" + SOH;
  size_t len = 0;
  EXPECT_FALSE(peek_full_message_len(buf, len));
}

TEST(PeekFullLenTest, ComputesLengthForMinimalMessage) {
  std::string buf = MakeMinimalFix();
  size_t len = 0;
  ASSERT_TRUE(peek_full_message_len(buf, len));

  // 수동 계산:
  // header_len = 위치(kBegin=0)부터 "9=5<SOH>" 끝까지 길이
  // "8=FIX.4.2<SOH>9=5<SOH>" = 9 + 1 + 3 + 1 = 14
  // body_len = 5
  // checksum 고정 7 ("10=" + 3자리 + SOH)
  // 총합 = 14 + 5 + 7 = 26
  EXPECT_EQ(len, 26u);
  EXPECT_EQ(buf.size(), 26u);
}

TEST(ExtractNextMessageTest, ExtractsSingleCompleteMessage) {
  std::string buf = MakeMinimalFix();
  std::string msg;
  EXPECT_TRUE(extract_next_message(buf, msg));
  EXPECT_TRUE(buf.empty());
  EXPECT_EQ(msg, MakeMinimalFix());
}

TEST(ExtractNextMessageTest, ReturnsFalseOnNoHeaderAndClearsBuffer) {
  std::string buf = "blahblah";
  std::string msg;
  EXPECT_FALSE(extract_next_message(buf, msg));
  EXPECT_FALSE(buf.empty());
  EXPECT_TRUE(msg.empty());
}

TEST(ExtractNextMessageTest, ReturnsFalseWhenOnlyPartialMessagePresent) {
  const std::string SOH = "\x01";
  // header와 9= 존재하지만 본문이 모자람
  std::string partial =
      "8=FIX.4.2" + SOH + "9=5" + SOH + "35=0";  // SOH, 10= 없음
  std::string buf = partial;
  std::string msg;
  EXPECT_FALSE(extract_next_message(buf, msg));
  // strip_to_header는 통과해서 buffer는 그대로 남음 (clear되지 않음)
  EXPECT_EQ(buf, partial);
  EXPECT_TRUE(msg.empty());
}

TEST(ExtractNextMessageTest, SkipsGarbageThenExtracts) {
  std::string valid = MakeMinimalFix();
  std::string buf = "noise_noise" + valid;
  std::string msg;
  EXPECT_TRUE(extract_next_message(buf, msg));
  EXPECT_EQ(msg, valid);
  EXPECT_TRUE(buf.empty());
}

TEST(ExtractNextMessageTest,
     ExtractsFirstAndLeavesRemainderForMultipleMessages) {
  std::string m1 = MakeMinimalFix();
  std::string m2 = MakeMinimalFix();
  std::string buf = m1 + m2;

  std::string msg;
  ASSERT_TRUE(extract_next_message(buf, msg));
  EXPECT_EQ(msg, m1);
  EXPECT_EQ(buf, m2);

  std::string msg2;
  ASSERT_TRUE(extract_next_message(buf, msg2));
  EXPECT_EQ(msg2, m2);
  EXPECT_TRUE(buf.empty());
}

TEST(ExtractNextMessageTest, HandlesLargeBodyLengthIfBufferHasEnoughData) {
  const std::string SOH = "\x01";
  std::string body = "35=0" + SOH + "49=X" + SOH;  // 5 + 5 = 10
  std::string msg = "8=FIX.4.4" + SOH + "9=10" + SOH + body + "10=000" + SOH;

  std::string buf = msg;
  std::string out;
  EXPECT_TRUE(extract_next_message(buf, out));
  EXPECT_EQ(out, msg);
  EXPECT_TRUE(buf.empty());
}

TEST(ExtractNextMessageTest, ExtractsTwoConcatenatedRealWorldMessages) {
  const std::string m1 =
      "8=FIX.4.4\x01"
      "9=0000194\x01"
      "35=X\x01"
      "49=SPOT\x01"
      "56=BMDWATCH\x01"
      "34=32261\x01"
      "52=20250909-12:07:12.537948\x01"
      "262=DEPTH_STREAM\x01"
      "268=1\x01"
      "279=0\x01"
      "269=2\x01"
      "270=112649.04000000\x01"
      "271=0.00887000\x01"
      "55=BTCUSDT\x01"
      "1003=5222475611\x01"
      "60=20250909-12:07:12.536335\x01"
      "2446=2\x01"
      "10=170\x01";

  const std::string m2 =
      "8=FIX.4.4\x01"
      "9=0000194\x01"
      "35=X\x01"
      "49=SPOT\x01"
      "56=BMDWATCH\x01"
      "34=32262\x01"
      "52=20250909-12:07:12.539960\x01"
      "262=DEPTH_STREAM\x01"
      "268=1\x01"
      "279=0\x01"
      "269=2\x01"
      "270=112649.04000000\x01"
      "271=0.00081000\x01"
      "55=BTCUSDT\x01"
      "1003=5222475612\x01"
      "60=20250909-12:07:12.538874\x01"
      "2446=2\x01"
      "10=164\x01";

  std::string buffer = m1 + m2;

  // 1) 첫 번째 메시지 길이 미리 산출 가능 여부
  size_t len_first = 0;
  ASSERT_TRUE(peek_full_message_len(buffer, len_first));
  EXPECT_EQ(len_first, m1.size()) << "peek_full_message_len이 첫 번째 메시지 "
                                     "길이를 정확히 계산해야 합니다.";

  // 2) 첫 번째 메시지 추출 후 버퍼에 두 번째 메시지가 남는지 확인
  std::string out1;
  ASSERT_TRUE(extract_next_message(buffer, out1));
  EXPECT_EQ(out1, m1);
  EXPECT_EQ(buffer, m2);

  // 3) 두 번째 메시지 길이 체크 및 추출
  size_t len_second = 0;
  ASSERT_TRUE(peek_full_message_len(buffer, len_second));
  EXPECT_EQ(len_second, m2.size());

  std::string out2;
  ASSERT_TRUE(extract_next_message(buffer, out2));
  EXPECT_EQ(out2, m2);

  // 4) 모두 소비되었는지 확인
  EXPECT_TRUE(buffer.empty());
}

TEST(ExtractNextMessageTest, ExtractsThreeConcatenatedRealWorldMessages) {
  const std::string m1 =
      "8=FIX.4.4\x01"
      "9=0000194\x01"
      "35=X\x01"
      "49=SPOT\x01"
      "56=BMDWATCH\x01"
      "34=32261\x01"
      "52=20250909-12:07:12.537948\x01"
      "262=DEPTH_STREAM\x01"
      "268=1\x01"
      "279=0\x01"
      "269=2\x01"
      "270=112649.04000000\x01"
      "271=0.00887000\x01"
      "55=BTCUSDT\x01"
      "1003=5222475611\x01"
      "60=20250909-12:07:12.536335\x01"
      "2446=2\x01"
      "10=170\x01";

  const std::string m2 =
      "8=FIX.4.4\x01"
      "9=0000194\x01"
      "35=X\x01"
      "49=SPOT\x01"
      "56=BMDWATCH\x01"
      "34=32262\x01"
      "52=20250909-12:07:12.539960\x01"
      "262=DEPTH_STREAM\x01"
      "268=1\x01"
      "279=0\x01"
      "269=2\x01";
  const std::string m3 =
      "270=112649.04000000\x01"
      "271=0.00081000\x01"
      "55=BTCUSDT\x01"
      "1003=5222475612\x01"
      "60=20250909-12:07:12.538874\x01"
      "2446=2\x01"
      "10=164\x01";

  std::string buffer = m1 + m2;

  // 1) 첫 번째 메시지 길이 미리 산출 가능 여부
  size_t len_first = 0;
  ASSERT_TRUE(peek_full_message_len(buffer, len_first));
  EXPECT_EQ(len_first, m1.size()) << "peek_full_message_len이 첫 번째 메시지 "
                                     "길이를 정확히 계산해야 합니다.";

  // 2) 첫 번째 메시지 추출 후 버퍼에 두 번째 메시지가 남는지 확인
  std::string out1;
  ASSERT_TRUE(extract_next_message(buffer, out1));
  EXPECT_EQ(out1, m1);
  EXPECT_EQ(buffer, m2);

  // 3) 두 번째 메시지 길이 체크 및 실패 확인
  std::string out2;
  std::string tmp = buffer;
  ASSERT_FALSE(extract_next_message(buffer, out2));
  ASSERT_EQ(tmp, buffer);

  // 4) 데이터 추가되고, 다시 메세지 추출 문제없는지 확인
  std::string out3;
  buffer.append(m3);
  ASSERT_TRUE(extract_next_message(buffer, out3));
  EXPECT_EQ(out3, m2 + m3);
  EXPECT_TRUE(buffer.empty());
}

TEST(ExtractNextMessageTest, SkipsLeadingGarbageThenExtractsTwoMessages) {
  const std::string m1 =
      "8=FIX.4.4\x01"
      "9=0000194\x01"
      "35=X\x01"
      "49=SPOT\x01"
      "56=BMDWATCH\x01"
      "34=32261\x01"
      "52=20250909-12:07:12.537948\x01"
      "262=DEPTH_STREAM\x01"
      "268=1\x01"
      "279=0\x01"
      "269=2\x01"
      "270=112649.04000000\x01"
      "271=0.00887000\x01"
      "55=BTCUSDT\x01"
      "1003=5222475611\x01"
      "60=20250909-12:07:12.536335\x01"
      "2446=2\x01"
      "10=170\x01";

  const std::string m2 =
      "8=FIX.4.4\x01"
      "9=0000194\x01"
      "35=X\x01"
      "49=SPOT\x01"
      "56=BMDWATCH\x01"
      "34=32262\x01"
      "52=20250909-12:07:12.539960\x01"
      "262=DEPTH_STREAM\x01"
      "268=1\x01"
      "279=0\x01"
      "269=2\x01"
      "270=112649.04000000\x01"
      "271=0.00081000\x01"
      "55=BTCUSDT\x01"
      "1003=5222475612\x01"
      "60=20250909-12:07:12.538874\x01"
      "2446=2\x01"
      "10=164\x01";

  std::string buffer = "GARBAGE\x02\x03" + m1 + m2;  // 헤더 이전 잡음

  // strip_to_header가 선행 garbage를 제거하는지
  ASSERT_TRUE(strip_to_header(buffer));
  ASSERT_TRUE(buffer.rfind("8=FIX", 0) == 0);

  // 두 메시지 연속 추출
  std::string out;
  ASSERT_TRUE(extract_next_message(buffer, out));
  EXPECT_EQ(out, m1);
  ASSERT_TRUE(extract_next_message(buffer, out));
  EXPECT_EQ(out, m2);
  EXPECT_TRUE(buffer.empty());
}

TEST(ExtractNextMessageTest, MergeMessageSegmentsAndExtractOneMessage) {
  const std::string m1 = "8=F";
  const std::string m2 =
      "IX.4.4\x01"
      "9=0000194\x01"
      "35=X\x01"
      "49=SPOT\x01"
      "56=BMDWATCH\x01"
      "34=32261\x01"
      "52=20250909-12:07:12.537948\x01"
      "262=DEPTH_STREAM\x01"
      "268=1\x01"
      "279=0\x01"
      "269=2\x01"
      "270=112649.04000000\x01"
      "271=0.00887000\x01"
      "55=BTCUSDT\x01"
      "1003=5222475611\x01"
      "60=20250909-12:07:12.536335\x01"
      "2446=2\x01"
      "10=170\x01";

  std::string buffer = m1;

  std::string out1;
  ASSERT_FALSE(extract_next_message(buffer, out1));
  EXPECT_FALSE(buffer.empty());
  EXPECT_TRUE(out1.empty());

  buffer.append(m2);

  std::string out2;
  ASSERT_TRUE(extract_next_message(buffer, out2));
  EXPECT_TRUE(buffer.empty());
  EXPECT_EQ(m1 + m2, out2);
}
}  // namespace core