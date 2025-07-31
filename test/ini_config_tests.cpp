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
#include "ini_config.hpp"

TEST(IniConfigTest, LoadAndReadValues) {
  IniConfig config;
  ASSERT_TRUE(config.load("resources/config.ini"));

  EXPECT_EQ(config.get_int("risk", "max_order_size"), 100);
  EXPECT_EQ(config.get_int("risk", "max_position"), 1);
  EXPECT_EQ(config.get_int("risk", "max_loss"), -100);

  EXPECT_EQ(config.get("database", "username", "root"), "root");
}