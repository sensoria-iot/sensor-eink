#include <FastEPD.h>
#include "../config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

// SCD4x
#include "scd4x_i2c.h"
#include "sensirion_common.h"
#include "sensirion_i2c_hal.h"

#include <fast/Roboto_Black_40.h>

static FASTEPD *epaper = nullptr;
static const char *TAG = "calibration";
#define BUTTON 28 // Not really used this time

extern "C" { void app_main(); }

void app_main()
{
    epaper = new FASTEPD();
    epaper->initPanel(BB_PANEL_SENSORIA_C5);
    epaper->fillScreen(BBEP_WHITE);
    epaper->setTextColor(BBEP_BLACK);
    epaper->setFont(Roboto_Black_40);

    esp_err_t rc = sensirion_i2c_hal_init(CONFIG_SDA_GPIO, CONFIG_SCL_GPIO);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "sensirion_i2c_hal_init failed: %s", esp_err_to_name(rc));
        epaper->drawString("I2C init failed", 10, 160);
        epaper->fullUpdate();
        vTaskDelay(portMAX_DELAY);
    }

    // Clean up potential SCD40 states (now safe: I2C is initialized)
    scd4x_wake_up();
    scd4x_stop_periodic_measurement();
    scd4x_reinit();

    // Disable ASC before starting measurement
    scd4x_set_automatic_self_calibration(0);

    // Start periodic measurement to let it settle in outdoor air
    rc = scd4x_start_periodic_measurement();
    if (rc != 0) {
        ESP_LOGE(TAG, "Error executing scd4x_start_periodic_measurement(): %d", rc);
        epaper->drawString("SCD4x start failed", 10, 160);
        epaper->fullUpdate();
        vTaskDelay(portMAX_DELAY);
    }

    epaper->drawString("SCD4x found!", 10, 20);
    epaper->drawString("Place sensor outside.", 10, 80);
    epaper->drawString("Calibration starts in 3s", 10, 140);
    epaper->fullUpdate();
    vTaskDelay(pdMS_TO_TICKS(3000));

    int iTime = 30 * 7; // 3.5 minutes
    char text[20];
    while (iTime > 0) {
        epaper->fillScreen(BBEP_WHITE);
        epaper->drawString("Running calibration", 10, 150);
        epaper->drawString("Time remaining:", 10, 200);
        sprintf(text, "%02d:%02d\n", iTime / 60, iTime % 60);
        epaper->drawString(text, 10, 260);
        printf("%s\n", text);

        vTaskDelay(pdMS_TO_TICKS(5000));
        iTime -= 5;
        epaper->partialUpdate(false);
    }

    // Stop periodic measurement before FRC
    scd4x_stop_periodic_measurement();
    vTaskDelay(pdMS_TO_TICKS(1000));

    uint16_t frc_correction = 0;
    rc = scd4x_perform_forced_recalibration(430, &frc_correction);

    if (rc == 0 && frc_correction != 0xFFFF) {
        epaper->drawString("Calibration Succeeded!", 10, 260);
        printf("Calibration Succeeded! frc_correction=%u\n", frc_correction);
    } else {
        epaper->drawString("Calibration failed", 10, 260);
        printf("Calibration failed. rc=%d frc_correction=0x%04X\n", rc, frc_correction);
    }
    epaper->partialUpdate(false);

    // Optional: release display resources if you want the absolute lowest idle current after running
    // epaper.deInit();

    vTaskDelay(portMAX_DELAY);
}