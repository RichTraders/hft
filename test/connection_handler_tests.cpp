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

#include "websocket/connection_handler.h"
#include "websocket/order_entry/exchanges/binance/spot/binance_spot_oe_connection_handler.h"
#include "websocket/order_entry/exchanges/binance/futures/binance_futures_oe_connection_handler.h"
#include "websocket/market_data/exchanges/binance/spot/binance_md_connection_handler.h"
#include "websocket/market_data/exchanges/binance/futures/binance_futures_md_connection_handler.h"

namespace {

struct MockOeApp {
  using WireMessage = std::monostate;

  bool send(const std::string& msg) {
    sent_messages.push_back(msg);
    return true;
  }

  std::string create_user_data_stream_subscribe() const {
    return "user_data_stream_subscribe";
  }

  void initiate_session_logon() {
    logon_initiated = true;
  }

  void handle_listen_key_response(const std::string& key) {
    listen_key = key;
  }

  void start_listen_key_keepalive() {
    keepalive_started = true;
  }

  std::vector<std::string> sent_messages;
  bool logon_initiated = false;
  std::string listen_key;
  bool keepalive_started = false;
};

struct MockMdApp {
  using WireMessage = std::monostate;

  void dispatch(const std::string& type, WireMessage) {
    dispatched_types.push_back(type);
  }

  std::vector<std::string> dispatched_types;
};

struct MockSessionLogonResponse {
  int status = 200;
};

struct MockUserSubscriptionResponse {
  int status = 200;
  struct Result {
    std::string listen_key = "test_listen_key";
  };
  std::optional<Result> result = Result{};
};

}  // namespace

class SpotOeConnectionHandlerTest : public ::testing::Test {
 protected:
  MockOeApp app_;
};

class FuturesOeConnectionHandlerTest : public ::testing::Test {
 protected:
  MockOeApp app_;
};

class MdConnectionHandlerTest : public ::testing::Test {
 protected:
  MockMdApp app_;
};

TEST_F(SpotOeConnectionHandlerTest, OnConnected_Api_InitiatesLogon) {
  core::ConnectionContext ctx(&app_, core::TransportId::kApi);

  BinanceSpotOeConnectionHandler::on_connected(ctx, core::TransportId::kApi);

  EXPECT_TRUE(app_.logon_initiated);
}

TEST_F(SpotOeConnectionHandlerTest, OnConnected_Stream_NoAction) {
  core::ConnectionContext ctx(&app_, core::TransportId::kStream);

  BinanceSpotOeConnectionHandler::on_connected(ctx, core::TransportId::kStream);

  EXPECT_FALSE(app_.logon_initiated);
}

TEST_F(SpotOeConnectionHandlerTest, OnSessionLogon_Success_SendsSubscribe) {
  core::ConnectionContext ctx(&app_, core::TransportId::kApi);
  MockSessionLogonResponse response{.status = 200};

  BinanceSpotOeConnectionHandler::on_session_logon(ctx, response);

  ASSERT_EQ(app_.sent_messages.size(), 1);
  EXPECT_EQ(app_.sent_messages[0], "user_data_stream_subscribe");
}

TEST_F(SpotOeConnectionHandlerTest, OnSessionLogon_Failure_NoAction) {
  core::ConnectionContext ctx(&app_, core::TransportId::kApi);
  MockSessionLogonResponse response{.status = 400};

  BinanceSpotOeConnectionHandler::on_session_logon(ctx, response);

  EXPECT_TRUE(app_.sent_messages.empty());
}

TEST_F(FuturesOeConnectionHandlerTest, OnConnected_Api_InitiatesLogon) {
  core::ConnectionContext ctx(&app_, core::TransportId::kApi);

  BinanceFuturesOeConnectionHandler::on_connected(ctx, core::TransportId::kApi);

  // Futures handler only initiates logon on API connect
  // User data stream subscription happens via separate callback after logon
  EXPECT_TRUE(app_.logon_initiated);
  EXPECT_TRUE(app_.sent_messages.empty());
}

TEST_F(FuturesOeConnectionHandlerTest, OnConnected_Stream_StartsKeepalive) {
  core::ConnectionContext ctx(&app_, core::TransportId::kStream);

  BinanceFuturesOeConnectionHandler::on_connected(ctx, core::TransportId::kStream);

  EXPECT_TRUE(app_.keepalive_started);
  EXPECT_FALSE(app_.logon_initiated);
}

TEST_F(FuturesOeConnectionHandlerTest, OnUserSubscription_Success_HandlesListenKey) {
  core::ConnectionContext ctx(&app_, core::TransportId::kApi);
  MockUserSubscriptionResponse response;

  BinanceFuturesOeConnectionHandler::on_user_subscription(ctx, response);

  EXPECT_EQ(app_.listen_key, "test_listen_key");
}

TEST_F(FuturesOeConnectionHandlerTest, OnUserSubscription_Failure_NoAction) {
  core::ConnectionContext ctx(&app_, core::TransportId::kApi);
  MockUserSubscriptionResponse response{.status = 400};

  BinanceFuturesOeConnectionHandler::on_user_subscription(ctx, response);

  EXPECT_TRUE(app_.listen_key.empty());
}

TEST_F(MdConnectionHandlerTest, SpotOnConnected_DispatchesLogonType) {
  core::ConnectionContext ctx(&app_, core::TransportId::kStream);

  BinanceMdConnectionHandler::on_connected(ctx, core::TransportId::kStream);

  ASSERT_EQ(app_.dispatched_types.size(), 1);
  EXPECT_EQ(app_.dispatched_types[0], "A");
}

TEST_F(MdConnectionHandlerTest, SpotOnConnected_ApiTransport_AlsoDispatches) {
  core::ConnectionContext ctx(&app_, core::TransportId::kApi);

  BinanceMdConnectionHandler::on_connected(ctx, core::TransportId::kApi);

  ASSERT_EQ(app_.dispatched_types.size(), 1);
  EXPECT_EQ(app_.dispatched_types[0], "A");
}

TEST_F(MdConnectionHandlerTest, FuturesOnConnected_DispatchesLogonType) {
  core::ConnectionContext ctx(&app_, core::TransportId::kStream);

  BinanceFuturesMdConnectionHandler::on_connected(ctx, core::TransportId::kStream);

  ASSERT_EQ(app_.dispatched_types.size(), 1);
  EXPECT_EQ(app_.dispatched_types[0], "A");
}

TEST_F(MdConnectionHandlerTest, FuturesOnConnected_ApiTransport_AlsoDispatches) {
  core::ConnectionContext ctx(&app_, core::TransportId::kApi);

  BinanceFuturesMdConnectionHandler::on_connected(ctx, core::TransportId::kApi);

  ASSERT_EQ(app_.dispatched_types.size(), 1);
  EXPECT_EQ(app_.dispatched_types[0], "A");
}

TEST(ConnectionContextTest, SendDelegatesToApp) {
  MockOeApp app;
  core::ConnectionContext ctx(&app, core::TransportId::kApi);

  bool result = ctx.send("test_message");

  EXPECT_TRUE(result);
  ASSERT_EQ(app.sent_messages.size(), 1);
  EXPECT_EQ(app.sent_messages[0], "test_message");
}

TEST(ZeroCostAbstractionTest, AllHandlersAreStaticAndTemplated) {
  static_assert(std::is_same_v<
      decltype(&BinanceSpotOeConnectionHandler::on_connected<MockOeApp>),
      void(*)(core::ConnectionContext<MockOeApp>&, core::TransportId)>);

  static_assert(std::is_same_v<
      decltype(&BinanceFuturesOeConnectionHandler::on_connected<MockOeApp>),
      void(*)(core::ConnectionContext<MockOeApp>&, core::TransportId)>);

  static_assert(std::is_same_v<
      decltype(&BinanceMdConnectionHandler::on_connected<MockMdApp>),
      void(*)(core::ConnectionContext<MockMdApp>&, core::TransportId)>);

  static_assert(std::is_same_v<
      decltype(&BinanceFuturesMdConnectionHandler::on_connected<MockMdApp>),
      void(*)(core::ConnectionContext<MockMdApp>&, core::TransportId)>);
}

TEST_F(FuturesOeConnectionHandlerTest, OnUserSubscription_EmptyListenKey_NoAction) {
  core::ConnectionContext ctx(&app_, core::TransportId::kApi);
  MockUserSubscriptionResponse response;
  response.result.value().listen_key = "";

  BinanceFuturesOeConnectionHandler::on_user_subscription(ctx, response);

  EXPECT_TRUE(app_.listen_key.empty());
}

TEST_F(FuturesOeConnectionHandlerTest, OnUserSubscription_NoResult_NoAction) {
  core::ConnectionContext ctx(&app_, core::TransportId::kApi);
  MockUserSubscriptionResponse response;
  response.result = std::nullopt;

  BinanceFuturesOeConnectionHandler::on_user_subscription(ctx, response);

  EXPECT_TRUE(app_.listen_key.empty());
}
