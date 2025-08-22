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
  ASSERT_TRUE(INI_CONFIG.load("resources/config.ini"));

  EXPECT_EQ(INI_CONFIG.get_double("risk", "max_order_size"), 0.0001);
  EXPECT_EQ(INI_CONFIG.get_double("risk", "max_position"), 0.0001);
  EXPECT_EQ(INI_CONFIG.get_double("risk", "max_loss"), 0.3);

  EXPECT_EQ(INI_CONFIG.get_int("risk", "not_exist"), 0);
  EXPECT_EQ(INI_CONFIG.get("database", "username", "root"), "root");
}