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

namespace precision_defaults {
constexpr int kQtyPrecision = 3;
constexpr int kPricePrecision = 2;
constexpr double kQtyMultiplier = 10000.;
constexpr double kPriceMultiplier = 10.;
}  // namespace precision_defaults

class PrecisionConfig : public Singleton<PrecisionConfig> {
 public:
  void initialize() {
    qty_precision_ = INI_CONFIG.get_int("meta",
        "qty_precision",
        precision_defaults::kQtyPrecision);
    price_precision_ = INI_CONFIG.get_int("meta",
        "price_precision",
        precision_defaults::kPricePrecision);
    qty_multiplier_ = INI_CONFIG.get_double("meta",
        "qty_multiplier",
        precision_defaults::kQtyMultiplier);
    price_multiplier_ = INI_CONFIG.get_double("meta",
        "price_multiplier",
        precision_defaults::kPriceMultiplier);
    initialized_ = true;
  }

  [[nodiscard]] int qty_precision() const {
    return initialized_ ? qty_precision_ : precision_defaults::kQtyPrecision;
  }

  [[nodiscard]] int price_precision() const {
    return initialized_ ? price_precision_
                        : precision_defaults::kPricePrecision;
  }

  [[nodiscard]] double qty_multiplier() const {
    return initialized_ ? qty_multiplier_ : precision_defaults::kQtyMultiplier;
  }

  [[nodiscard]] double price_multiplier() const {
    return initialized_ ? price_multiplier_
                        : precision_defaults::kPriceMultiplier;
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
  int qty_precision_{precision_defaults::kQtyPrecision};
  int price_precision_{precision_defaults::kPricePrecision};
  double qty_multiplier_{precision_defaults::kQtyMultiplier};
  double price_multiplier_{precision_defaults::kPriceMultiplier};
  bool initialized_{false};
};

}  // namespace common

#define PRECISION_CONFIG common::PrecisionConfig::instance()

#endif  // PRECISION_CONFIG_HPP
