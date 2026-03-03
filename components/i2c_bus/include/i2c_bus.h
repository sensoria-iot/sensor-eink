#pragma once

#include "sdkconfig.h"

#if !CONFIG_IDF_TARGET_ESP32C5
// Hard fail at compile time if someone tries to build this project for a non-C5 target.
#error "sensor-eink-c5 supports only ESP32-C5 (CONFIG_IDF_TARGET_ESP32C5 must be enabled). Select target with: idf.py set-target esp32c5"
#endif

#include "esp_err.h"
#include <stdint.h>
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t i2c_bus_init(int sda_gpio, int scl_gpio);

/**
 * @brief Get (and cache) a device handle for a 7-bit address on the shared I2C bus.
 *
 * @param addr7 7-bit I2C address (e.g. 0x68)
 * @param speed_hz Bus speed for this device (e.g. 100000 or 400000)
 */
i2c_master_dev_handle_t i2c_bus_get_dev(uint8_t addr7, uint32_t speed_hz);

#ifdef __cplusplus
}
#endif