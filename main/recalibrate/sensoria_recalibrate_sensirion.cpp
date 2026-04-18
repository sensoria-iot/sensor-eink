#include <FastEPD.h>
// Edit your API setup and general configuration options:
#include "../config.h"
//#include <bb_scd41.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
// SCD4x
#include "scd4x_i2c.h"
#include "sensirion_common.h"
#include "sensirion_i2c_hal.h"

#include <fast/Roboto_Black_40.h>
FASTEPD epaper;
// Note: Larry example was recoded to use sensirion library
//SCD41 co2;
char *TAG = "calibration";
#define BUTTON 28


extern "C"
{
    void app_main();
}

void app_main() {
  int iTime;
  //pinMode(BUTTON, INPUT); // boot button
  epaper.initPanel(BB_PANEL_SENSORIA_C5);
  epaper.fillScreen(BBEP_WHITE);
  epaper.setTextColor(BBEP_BLACK);
  epaper.setFont(Roboto_Black_40);
  epaper.setCursor(0, 60);

      // int16_t sensirion_i2c_hal_init(int gpio_sda, int gpio_scl);
   esp_err_t rc = sensirion_i2c_hal_init(CONFIG_SDA_GPIO, CONFIG_SCL_GPIO);

    // Clean up potential SCD40 states
    scd4x_wake_up();
    scd4x_stop_periodic_measurement();
    scd4x_reinit();

  //int rc = co2.init(SDA_PIN, SCL_PIN);
  if (rc == ESP_OK) {
    ESP_LOGI(TAG, "SCD40 OK");
    //co2.start(); // start periodic updates
    //co2.setAutoCalibrate(false); // turn off auto-calibration (switches to manual calibration)
    esp_err_t co2_start = scd4x_start_periodic_measurement();
    scd4x_set_automatic_self_calibration(0); // turn off auto-calibration
    if (co2_start)
    {
        ESP_LOGE(TAG, "Error executing scd4x_start_periodic_measurement()");
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
    epaper.setCursor(20, 120);
    epaper.drawString("SCD4x found!", 10, 20);
    ESP_LOGW(TAG, "Place sensor outside. In 3 seconds calibration starts");
    epaper.setCursor(20, 200);
    epaper.drawString("Place sensor outside. In 3 seconds calibration starts", 10, 80);
    epaper.fullUpdate();
    //while (digitalRead(BUTTON) == 1) {
      vTaskDelay(pdMS_TO_TICKS(3000));
    //}
    iTime = 30*7; // 3.5 minutes
    char text[20];
    while (iTime > 0) {
      epaper.fillScreen(BBEP_WHITE);
      epaper.setCursor(0, 60);
      epaper.drawString("Running calibration", 10, 150);
      epaper.drawString("Time remaining:", 10, 200);
      sprintf(text, "%02d:%02d\n", iTime/60, iTime % 60);
      epaper.drawString(text, 10, 260);
      printf("%s \n", text);
      vTaskDelay(pdMS_TO_TICKS(5000));
      iTime -= 5; // show time in 5 second increments
      epaper.partialUpdate(false);
    }
    // try to recalibrate it to the ambient value of 430 ppm
    //rc = co2.recalibrate(430);
    rc = scd4x_perform_forced_recalibration(430, (uint16_t*)0);
    if (rc == 0) {
      epaper.drawString("Calibration Succeeded!", 10, 260);
      printf("Calibration Succeeded! \n");
    } else {
      epaper.drawString("Calibration failed", 10, 260);
      printf("Calibration failed. ERR: %d\n", rc);
    }
    epaper.partialUpdate(false);
  } else {
    epaper.drawString("SCD4x not found.", 10, 160);
    epaper.fullUpdate();
    epaper.deInit();
    while (1) {
      vTaskDelay(1);
    }
  }
}
