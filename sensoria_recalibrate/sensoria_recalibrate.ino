#include <FastEPD.h>
#include <bb_scd41.h>
#include <../Fonts/Roboto_Black_40.h>
FASTEPD epaper;
SCD41 co2;
// GPIOs for the Sensoria S3
#define SDA_PIN 39
#define SCL_PIN 40
#define BUTTON 0

void setup() {
  int iTime;
  pinMode(BUTTON, INPUT); // boot button
  epaper.initPanel(BB_PANEL_V7_RAW);
  epaper.setPanelSize(BBEP_DISPLAY_ED052TC4);
  epaper.fillScreen(BBEP_WHITE);
  epaper.setTextColor(BBEP_BLACK);
  epaper.setFont(Roboto_Black_40);
  epaper.setCursor(0, 60);
  int rc = co2.init(SDA_PIN, SCL_PIN);
  if (rc == SCD41_SUCCESS) {
    co2.start(); // start periodic updates
    co2.setAutoCalibrate(false); // turn off auto-calibration (switches to manual calibration)
    epaper.println("SCD4x found!");
    epaper.println("Place sensor outside.");
    epaper.println("Press boot button to");
    epaper.println("start calibration.");
    epaper.fullUpdate();
    while (digitalRead(BUTTON) == 1) {
      vTaskDelay(1);
    }
    iTime = 30*7; // 3.5 minutes
    while (iTime > 0) {
      epaper.fillScreen(BBEP_WHITE);
      epaper.setCursor(0, 60);
      epaper.println("Running calibration");
      epaper.println("Time remaining:");
      epaper.printf("%02d:%02d\n", iTime/60, iTime % 60);
      delay(5000);
      iTime -= 5; // show time in 5 second increments
      epaper.partialUpdate(false);
    }
    // try to recalibrate it to the ambient value of 430 ppm
    rc = co2.recalibrate(430);
    if (rc == SCD41_SUCCESS) {
      epaper.println("Calibration Succeeded!");
    } else {
      epaper.println("Calibration failed.");
    }
    epaper.partialUpdate(false);
  } else {
    epaper.println("SCD4x not found.");
    epaper.fullUpdate();
    epaper.deInit();
    while (1) {
      vTaskDelay(1);
    }
  }
} /* setup() */

void loop() {
}
