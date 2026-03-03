#include "../config.h"
#include "i2c_bus.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <time.h>
#include "bb_rtc.h"

static const char *TAG = "test_bb_rtc";

static void print_tm(const char *label, const struct tm *t)
{
    // tm_year is years since 1900; tm_mon is 0-11
    ESP_LOGI(TAG, "%s: %04d-%02d-%02d %02d:%02d:%02d (wday=%d yday=%d isdst=%d)",
             label,
             t->tm_year + 1900,
             t->tm_mon + 1,
             t->tm_mday,
             t->tm_hour,
             t->tm_min,
             t->tm_sec,
             t->tm_wday,
             t->tm_yday,
             t->tm_isdst);
}

static const char *rtc_type_name(int t)
{
    switch (t) {
        case RTC_PCF8563:   return "PCF8563";
        case RTC_DS3231:    return "DS3231";
        case RTC_RV3032:    return "RV3032";
        case RTC_PCF85063A: return "PCF85063A";
        default:            return "UNKNOWN";
    }
}

extern "C" void app_main(void)
{
    // Initialize shared bus once (bb_rtc init will also call i2c_bus_init internally in your branch,
    // but doing it here makes the dependency explicit and helps early failure reporting).
    ESP_ERROR_CHECK(i2c_bus_init(CONFIG_SDA_GPIO, CONFIG_SCL_GPIO));

    BBRTC rtc;

    // bWire=true means use the (ESP-IDF) I2C implementation path, not bitbang.
    int rc = rtc.init(CONFIG_SDA_GPIO, CONFIG_SCL_GPIO, true /*bWire*/, 100000 /*speed*/);
    if (rc != RTC_SUCCESS) {
        ESP_LOGE(TAG, "rtc.init failed (%d). Check wiring/pins/pullups.", rc);
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    int type = rtc.getType();
    ESP_LOGI(TAG, "RTC type=%d (%s)", type, rtc_type_name(type));

    // Read time
    struct tm now = {};
    rtc.getTime(&now);
    print_tm("RTC time (before)", &now);

    // Write: +10 seconds (mktime normalizes)
    struct tm test = now;
    test.tm_sec += 10;
    (void)mktime(&test); // normalize fields
    rtc.setTime(&test);

    vTaskDelay(pdMS_TO_TICKS(50));

    struct tm verify = {};
    rtc.getTime(&verify);
    print_tm("RTC time (after +10s write)", &verify);

    // Also show epoch API
    uint32_t epoch = rtc.getEpoch();
    ESP_LOGI(TAG, "RTC epoch=%" PRIu32, epoch);

    while (1) {
        struct tm t = {};
        rtc.getTime(&t);
        print_tm("Tick", &t);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}