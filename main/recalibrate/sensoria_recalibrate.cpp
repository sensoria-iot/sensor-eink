#include <FastEPD.h>
#include <bb_scd41.h>
#include "../config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <fast/Roboto_Black_40.h>
FASTEPD epaper;
SCD41 co2;
#define BUTTON 0


extern "C"
{
    void app_main();
}

void app_main() {
  int iTime;
  //pinMode(BUTTON, INPUT); // boot button
  epaper.initPanel(BB_PANEL_V7_RAW);
  epaper.setPanelSize(EPD_WIDTH, EPD_HEIGHT, BB_PANEL_FLAG_MIRROR_X);
  epaper.setRotation(180);
  epaper.fillScreen(BBEP_WHITE);
  epaper.setTextColor(BBEP_BLACK);
  epaper.setFont(Roboto_Black_40);
  epaper.setCursor(0, 60);
  int rc = co2.init(CONFIG_SDA_GPIO, CONFIG_SCL_GPIO);
  if (rc == SCD41_SUCCESS) {
    co2.start(); // start periodic updates
    co2.setAutoCalibrate(false); // turn off auto-calibration (switches to manual calibration)
    epaper.drawString("SCD4x found!", 10, 20);
    epaper.drawString("Place sensor outside. In 3 seconds start!", 10, 80);
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
    rc = co2.recalibrate(430);
    if (rc == SCD41_SUCCESS) {
      epaper.drawString("Calibration Succeeded!", 10, 260);
      printf("Calibration Succeeded! \n");
    } else {
      epaper.drawString("Calibration failed.", 10, 260);
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
