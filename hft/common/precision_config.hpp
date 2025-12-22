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

#ifndef PRECISION_CONFIG_HPP
#define PRECISION_CONFIG_HPP

#include "ini_config.hpp"
#include "singleton.h"

namespace common {

class PrecisionConfig : public Singleton<PrecisionConfig> {
 public:
  void initialize() {
    qty_precision_ = INI_CONFIG.get_int("meta", "qty_precision", 3);
    price_precision_ = INI_CONFIG.get_int("meta", "price_precision", 2);
    initialized_ = true;
  }

  [[nodiscard]] int qty_precision() const {
    return initialized_ ? qty_precision_ : 3;
  }

  [[nodiscard]] int price_precision() const {
    return initialized_ ? price_precision_ : 2;
  }

  void set_qty_precision(int precision) {
    qty_precision_ = precision;
    initialized_ = true;
  }

  void set_price_precision(int precision) {
    price_precision_ = precision;
    initialized_ = true;
  }

 private:
  int qty_precision_{3};
  int price_precision_{2};
  bool initialized_{false};
};

}  // namespace common

#define PRECISION_CONFIG common::PrecisionConfig::instance()

#endif  // PRECISION_CONFIG_HPP
