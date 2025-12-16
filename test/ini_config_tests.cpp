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

#include <filesystem>
#include <fstream>

#include "ini_config.hpp"

class IniConfigTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create test directory structure
    test_dir_ = std::filesystem::temp_directory_path() / "ini_config_test";
    std::filesystem::create_directories(test_dir_);
    std::filesystem::create_directories(test_dir_ / "symbol");
    std::filesystem::create_directories(test_dir_ / "strategy");
    std::filesystem::create_directories(test_dir_ / "env");
  }

  void TearDown() override {
    // Clean up test files
    std::filesystem::remove_all(test_dir_);
  }

  void write_file(const std::filesystem::path& path, const std::string& content) {
    std::ofstream file(path);
    file << content;
  }

  std::filesystem::path test_dir_;
};

TEST_F(IniConfigTest, LoadSingleFile) {
  write_file(test_dir_ / "config.ini", R"(
[section1]
key1 = value1
key2 = 42

[section2]
key3 = 3.14
)");

  ASSERT_TRUE(INI_CONFIG.load((test_dir_ / "config.ini").string()));

  EXPECT_EQ(INI_CONFIG.get("section1", "key1"), "value1");
  EXPECT_EQ(INI_CONFIG.get_int("section1", "key2"), 42);
  EXPECT_DOUBLE_EQ(INI_CONFIG.get_double("section2", "key3"), 3.14);
}

TEST_F(IniConfigTest, HasKey) {
  write_file(test_dir_ / "config.ini", R"(
[section1]
existing_key = value
)");

  ASSERT_TRUE(INI_CONFIG.load((test_dir_ / "config.ini").string()));

  EXPECT_TRUE(INI_CONFIG.has_key("section1", "existing_key"));
  EXPECT_FALSE(INI_CONFIG.has_key("section1", "non_existing_key"));
  EXPECT_FALSE(INI_CONFIG.has_key("non_existing_section", "key"));
}

TEST_F(IniConfigTest, LoadWithProfiles) {
  // Base config with profile section
  write_file(test_dir_ / "config.ini", R"(
[profile]
symbol = BTCUSDT
strategy = maker
environment = dev

[base]
base_value = from_base
override_value = base_default
)");

  // Symbol config
  write_file(test_dir_ / "symbol" / "config-BTCUSDT.ini", R"(
[meta]
ticker = BTCUSDT
price_precision = 1

[base]
override_value = from_symbol
)");

  // Strategy config
  write_file(test_dir_ / "strategy" / "config-maker.ini", R"(
[strategy]
algorithm = maker
threshold = 50

[base]
override_value = from_strategy
)");

  // Env config
  write_file(test_dir_ / "env" / "config-dev.ini", R"(
[log]
level = DEBUG
)");

  ASSERT_TRUE(INI_CONFIG.load((test_dir_ / "config.ini").string()));

  // Check profile info
  EXPECT_EQ(INI_CONFIG.get_active_symbol(), "BTCUSDT");
  EXPECT_EQ(INI_CONFIG.get_active_strategy(), "maker");
  EXPECT_EQ(INI_CONFIG.get_active_environment(), "dev");

  // Check base value
  EXPECT_EQ(INI_CONFIG.get("base", "base_value"), "from_base");

  // Check symbol config loaded
  EXPECT_EQ(INI_CONFIG.get("meta", "ticker"), "BTCUSDT");
  EXPECT_EQ(INI_CONFIG.get_int("meta", "price_precision"), 1);

  // Check strategy config loaded
  EXPECT_EQ(INI_CONFIG.get("strategy", "algorithm"), "maker");
  EXPECT_EQ(INI_CONFIG.get_int("strategy", "threshold"), 50);

  // Check env config loaded
  EXPECT_EQ(INI_CONFIG.get("log", "level"), "DEBUG");

  // Check priority: strategy > symbol > env > base
  // Strategy should override symbol which overrides base
  EXPECT_EQ(INI_CONFIG.get("base", "override_value"), "from_strategy");
}

TEST_F(IniConfigTest, LoadedFilesTracking) {
  write_file(test_dir_ / "config.ini", R"(
[profile]
symbol = TEST

[base]
key = value
)");

  write_file(test_dir_ / "symbol" / "config-TEST.ini", R"(
[meta]
ticker = TEST
)");

  ASSERT_TRUE(INI_CONFIG.load((test_dir_ / "config.ini").string()));

  const auto& loaded_files = INI_CONFIG.get_loaded_files();
  EXPECT_GE(loaded_files.size(), 2);

  // Base config should be first
  EXPECT_TRUE(loaded_files[0].find("config.ini") != std::string::npos);
}

TEST_F(IniConfigTest, MissingProfileFileIsOptional) {
  write_file(test_dir_ / "config.ini", R"(
[profile]
symbol = NONEXISTENT
strategy = missing

[base]
key = value
)");

  // Should succeed even if profile files don't exist
  ASSERT_TRUE(INI_CONFIG.load((test_dir_ / "config.ini").string()));
  EXPECT_EQ(INI_CONFIG.get("base", "key"), "value");
}

TEST_F(IniConfigTest, BackwardCompatibility_NoProfileSection) {
  write_file(test_dir_ / "config.ini", R"(
[section1]
key1 = value1

[section2]
key2 = value2
)");

  // Should work without [profile] section
  ASSERT_TRUE(INI_CONFIG.load((test_dir_ / "config.ini").string()));

  EXPECT_EQ(INI_CONFIG.get("section1", "key1"), "value1");
  EXPECT_EQ(INI_CONFIG.get("section2", "key2"), "value2");
  EXPECT_TRUE(INI_CONFIG.get_active_symbol().empty());
}

TEST_F(IniConfigTest, CustomProfileType) {
  write_file(test_dir_ / "config.ini", R"(
[profile]
symbol = BTCUSDT
custom_type = custom_value

[base]
key = base_value
)");

  // Create custom profile directory and file
  std::filesystem::create_directories(test_dir_ / "custom_type");
  write_file(test_dir_ / "custom_type" / "config-custom_value.ini", R"(
[custom]
custom_key = from_custom_profile
)");

  write_file(test_dir_ / "symbol" / "config-BTCUSDT.ini", R"(
[meta]
ticker = BTCUSDT
)");

  ASSERT_TRUE(INI_CONFIG.load((test_dir_ / "config.ini").string()));

  // Custom profile should be loaded
  EXPECT_EQ(INI_CONFIG.get("custom", "custom_key"), "from_custom_profile");
  EXPECT_EQ(INI_CONFIG.get("meta", "ticker"), "BTCUSDT");
}

TEST_F(IniConfigTest, GetWithSymbolPlaceholder) {
  write_file(test_dir_ / "config.ini", R"(
[profile]
symbol = BTCUSDT

[exchange]
md_ws_path = /stream?streams={symbol}@depth/{symbol}@aggTrade
oe_ws_path = /ws/{symbol}
no_placeholder = /static/path
)");

  ASSERT_TRUE(INI_CONFIG.load((test_dir_ / "config.ini").string()));

  // {symbol} should be replaced with lowercase symbol
  EXPECT_EQ(INI_CONFIG.get_with_symbol("exchange", "md_ws_path"),
            "/stream?streams=btcusdt@depth/btcusdt@aggTrade");
  EXPECT_EQ(INI_CONFIG.get_with_symbol("exchange", "oe_ws_path"),
            "/ws/btcusdt");

  // No placeholder - value unchanged
  EXPECT_EQ(INI_CONFIG.get_with_symbol("exchange", "no_placeholder"),
            "/static/path");

  // Non-existent key - returns default
  EXPECT_EQ(INI_CONFIG.get_with_symbol("exchange", "missing", "/default"),
            "/default");
}

TEST_F(IniConfigTest, GetWithSymbolNoActiveSymbol) {
  write_file(test_dir_ / "config.ini", R"(
[exchange]
path = /stream?streams={symbol}@depth
)");

  ASSERT_TRUE(INI_CONFIG.load((test_dir_ / "config.ini").string()));

  // No active symbol - placeholder not replaced
  EXPECT_EQ(INI_CONFIG.get_with_symbol("exchange", "path"),
            "/stream?streams={symbol}@depth");
}

// Legacy test - keep for backward compatibility
TEST(IniConfigLegacyTest, LoadProductionConfig) {
  // Only run if resources/config.ini exists
  if (!std::filesystem::exists("resources/config.ini")) {
    GTEST_SKIP() << "resources/config.ini not found";
  }

  ASSERT_TRUE(INI_CONFIG.load("resources/config.ini"));

  // Check that required sections exist (profile section is optional)
  // This test verifies backward compatibility - config without [profile] should still work
  EXPECT_FALSE(INI_CONFIG.get("meta", "ticker").empty());
}