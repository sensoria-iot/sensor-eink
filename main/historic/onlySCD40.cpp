// Edit your API setup and general configuration options:
#include "../config.h"


// FastEPD component:
#include "FastEPD.cpp"
FASTEPD epaper;
// Fonts
#include "fast/ubuntu12.h"
#include "fast/ubuntu20.h"
#include "fast/ubuntu30.h"
#include "fast/ubuntu40.h"
#include "fast/ubuntu_L_30.h"
// Icons
#include "fast/ico/ico_co2.h"
#include "fast/ico/ico_temp.h"
#include "fast/ico/ico_hum.h"
#include "fast/ico/arrow_up.h"
#include "fast/ico/arrow_down.h"
#include "fast/ico/arrow_neutral.h"
#include "fast/ico/alert.h"
#include "fast/ico/confidence_chart.h"
static const char *TAG = "CO2_ST";

#define DARKMODE false
#define LOW_BATT_ALERT 20
bool ready_to_measure = false;
bool measure_taken = false;
float tem = 0;

#include <math.h>  // roundf
// Non-Volatile Storage (NVS) - borrrowed from esp-idf/examples/storage/nvs_rw_value
#include "nvs_flash.h"
#include "nvs.h"
// Values that will be stored in NVS - defaults here
nvs_handle_t nvs_h;
uint16_t nvs_minutes_till_refresh = DEEP_SLEEP_MINUTES;
// General libs
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/param.h>
#include <stdlib.h>
#include <ctype.h>
// ESP + FreeRTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_log.h"
// ADC
#include "soc/soc_caps.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#define ADC2_CHANNEL ADC_CHANNEL_0  // IO1
#define ADC_ATTEN    ADC_ATTEN_DB_12
static int adc_raw[1][10];
static int voltage[1][10];
adc_oneshot_unit_handle_t adc1_handle;
adc_cali_handle_t adc1_cali_chan0_handle = NULL;
bool do_calibration1_chan0 = false;
int batt_level = 100;

// SCD4x
#include "scd4x_i2c.h"
#include "sensirion_common.h"
#include "sensirion_i2c_hal.h"

// Flag to know that how many times the device booted
int16_t nvs_boots = 0;
// EPD framebuffer
uint8_t *fb;

extern "C"
{
    void app_main();
}

void deep_sleep()
{
    printf("15 seconds wait before sleep\n");
    vTaskDelay(15000 / portTICK_PERIOD_MS);
    
    ESP_LOGI(pcTaskGetName(0), "DEEP_SLEEP_MINUTES: %d mins to wake-up", nvs_minutes_till_refresh);
    esp_deep_sleep(1000000LL * 60 * nvs_minutes_till_refresh);
}

int adc_read_batt() {
    int batt = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC2_CHANNEL, &adc_raw[0][0]));
    ESP_LOGI(TAG, "ADC%d Channel[%d] Raw Data: %d", ADC_UNIT_1 + 1, ADC2_CHANNEL, adc_raw[0][0]);
    if (do_calibration1_chan0) {
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_chan0_handle, adc_raw[0][0], &voltage[0][0]));
        // TODO: Use different formula for this
        batt = ((adc_raw[0][0]*0.1) - 55) *3;
        ESP_LOGI(TAG, "ADC%d Channel[%d] Cali Voltage: %d mV BATT: %d %%", ADC_UNIT_1 + 1, ADC2_CHANNEL, voltage[0][0], batt);
    }
    return batt;
}

uint16_t generateRandom(uint16_t max)
{
    if (max > 0)
    {
        srand(esp_timer_get_time());
        return rand() % max;
    }
    return 0;
}

void logo_sensoria(int x, int y)
{
    uint32_t bg_color = 0xF;
    uint32_t fg_color = 0;
    #if DARKMODE
      bg_color = 0;
      fg_color = 0xF;
    #endif
    epaper.fillCircle(x, y, 100, fg_color);
    epaper.fillRect(x, y-100, 100, 200, bg_color); // Half Circle
    epaper.drawCircle(x, y, 100, fg_color);
    epaper.drawCircle(x, y, 80, fg_color);
    epaper.drawCircle(x, y, 60, fg_color);
    epaper.drawCircle(x, y, 40, fg_color);
    epaper.drawCircle(x, y, 20, fg_color);
    epaper.drawCircle(x, y, 99, fg_color); // Double lines
    epaper.drawCircle(x, y, 79, fg_color);
    epaper.drawCircle(x, y, 59, fg_color);
    epaper.drawCircle(x, y, 39, fg_color);
    epaper.drawCircle(x, y, 19, fg_color);
}

void scd_render_co2(uint16_t co2, int x, int y)
{
    //logo_sensoria(200, 170);
    epaper.loadG5Image(ico_co2, x-108, y-58, 0xF, 0x0);
    epaper.setFont(ubuntu40);
    char textbuffer[20];
    snprintf(textbuffer, sizeof(textbuffer), "%d", co2);
    epaper.drawString(textbuffer, x, y);

    epaper.setFont(ubuntu20);
    snprintf(textbuffer, sizeof(textbuffer), "ppm");
    x += (co2 < 1000) ? 150 : 200;
    epaper.drawString(textbuffer, x, y);
    epaper.setFont(ubuntu40);
    
}

void scd_render_temp(double temp, int x, int y)
{
    char textbuffer[12];
    snprintf(textbuffer, sizeof(textbuffer), "%.1f °C", temp);
    epaper.drawString(textbuffer, x, y);

    epaper.loadG5Image(ico_temp, x-60, y-46, 0xF, 0x0);
}

void scd_render_h(double hum, int x, int y)
{
    epaper.loadG5Image(ico_hum, x-80, y-50, 0xF, 0x0);
    char textbuffer[12];
    snprintf(textbuffer, sizeof(textbuffer), "%.1f%%", hum);
    epaper.drawString(textbuffer, x, y);
}

void epd_print_error(char *message)
{
    int x = 100; // EPD_WIDTH/2-300
    int y = 20;
    epaper.drawString(message, x, y);

    epaper.fullUpdate(true, false);
    vTaskDelay(200);
    deep_sleep();
}

void read_batt_level() {
   batt_level = adc_read_batt();

   if (batt_level < LOW_BATT_ALERT) {
    epaper.setFont(ubuntu_L_30);
    epaper.drawString("< CHARGE", 40, EPD_HEIGHT - 130);
   }
   if (batt_level > 100) {
    batt_level = 100;
   }
   batt_level = batt_level*0.8;

   int color = 0;
   #if DARKMODE
   color = 0xF;
   #endif
   int x_offset = EPD_WIDTH - 160;
   int y_offset = 70;
   epaper.drawRect(x_offset, y_offset - 50, 80, 30, color);
   epaper.drawRect(x_offset, y_offset - 50, 81, 31, color);
   epaper.drawRect(x_offset +1, y_offset - 51, 80, 30, color);
   epaper.drawRect(x_offset +80, y_offset - 41, 10, 12, color);
   epaper.drawRect(x_offset +81, y_offset - 42, 10, 12, color);
   epaper.fillRect(x_offset +1, y_offset - 49, batt_level-1, 28, 0xA); // bar
   
   /* epaper.setFont(ubuntu20); //print %
   char textbuffer[12];
   snprintf(textbuffer, sizeof(textbuffer), "%d", batt_level); // %%
   epaper.drawString(textbuffer, x_offset, y_offset - 20); */
}

void scd_read()
{
    int16_t error = 0;
    // int16_t sensirion_i2c_hal_init(int gpio_sda, int gpio_scl);
    sensirion_i2c_hal_init(CONFIG_SDA_GPIO, CONFIG_SCL_GPIO);

    // Clean up potential SCD40 states
    scd4x_wake_up();
    scd4x_stop_periodic_measurement();
    scd4x_reinit();

    uint16_t serial_0;
    uint16_t serial_1;
    uint16_t serial_2;
    error = scd4x_get_serial_number(&serial_0, &serial_1, &serial_2);
    if (error)
    {
        printf("Error executing scd4x_get_serial_number(): %i\n", error);
    }
    else
    {
        ESP_LOGI(TAG, "serial: 0x%04x%04x%04x\n", serial_0, serial_1, serial_2);
    }

    // Start Measurement
    error = scd4x_start_periodic_measurement();
    if (error)
    {
        ESP_LOGE(TAG, "Error executing scd4x_start_periodic_measurement(): %i\n", error);
        epd_print_error((char *)"Please insert sensor");
        deep_sleep();
    }

    printf("Waiting for first measurement... (5 sec)\n");
    bool data_ready_flag = false;
    for (uint8_t c = 0; c < 100; ++c)
    {
        // Read Measurement
        sensirion_i2c_hal_sleep_usec(100000);
        // bool data_ready_flag = false;
        error = scd4x_get_data_ready_flag(&data_ready_flag);
        if (error)
        {
            ESP_LOGE(TAG, "Error executing scd4x_get_data_ready_flag(): %i\n", error);
            continue;
        }
        if (data_ready_flag)
        {
            measure_taken = true;
            break;
        }
    }
    if (!data_ready_flag)
    {
        ESP_LOGE(TAG, "SCD4x ready flag is not coming in time");
    }

    uint16_t co2;
    int32_t temperature;
    int32_t humidity;
    error = scd4x_read_measurement(&co2, &temperature, &humidity);
    if (error)
    {
        ESP_LOGE(TAG, "Error executing scd4x_read_measurement(): %i\n", error);
    }
    else if (co2 == 0)
    {
        ESP_LOGI(TAG, "Invalid sample detected, skipping.\n");
    }
    else
    {
        scd4x_stop_periodic_measurement();
        tem = (float)temperature / 1000;
        tem = roundf(tem * 10) / 10;
        float hum = (float)humidity / 1000;
        hum = roundf(hum * 10) / 10;
        ESP_LOGI(TAG, "CO2 : %u", co2);
        ESP_LOGI(TAG, "Temp: %d m°C %.1f 0xFC", (int)temperature, tem);
        ESP_LOGI(TAG, "Humi: %d mRH %.1f %%\n", (int)humidity, hum);

        int cursor_x = 140;
        int cursor_y = EPD_HEIGHT-80;

        scd_render_co2(co2, cursor_x, cursor_y);

        cursor_x = EPD_WIDTH/2 - 70;
        scd_render_temp(tem, cursor_x, cursor_y);

        cursor_x = EPD_WIDTH - 300;
        scd_render_h(hum, cursor_x, cursor_y);

        read_batt_level();
        logo_sensoria(EPD_WIDTH/2 +50, 300);
        //epaper.loadG5Image(rainmaker, EPD_WIDTH-320, 50, 0x0, 0xF);
        epaper.fullUpdate(true, false);
    }

    vTaskDelay(pdMS_TO_TICKS(3000));
    ESP_LOGI(TAG, "scd4x_power_down()");
    scd4x_power_down();
    sensirion_i2c_hal_free();

     deep_sleep();
}

/*---------------------------------------------------------------
        ADC Calibration
---------------------------------------------------------------*/
static bool adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Line Fitting");
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibration Success");
    } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
        ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
    } else {
        ESP_LOGE(TAG, "Invalid arg or no memory");
    }

    return calibrated;
}

static void adc_calibration_deinit(adc_cali_handle_t handle)
{
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    ESP_LOGI(TAG, "deregister %s calibration scheme", "Curve Fitting");
    ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(handle));

#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    ESP_LOGI(TAG, "deregister %s calibration scheme", "Line Fitting");
    ESP_ERROR_CHECK(adc_cali_delete_scheme_line_fitting(handle));
#endif
}

void app_main()
{
    //-------------ADC1 Init---------------//
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    //-------------ADC1 Config---------------//
    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC2_CHANNEL, &config));

    //-------------ADC1 Calibration Init---------------//
    do_calibration1_chan0 = adc_calibration_init(ADC_UNIT_1, ADC2_CHANNEL, ADC_ATTEN, &adc1_cali_chan0_handle);

    esp_err_t err;
    // WiFi log level
    esp_log_level_set("wifi", ESP_LOG_ERROR);
    epaper.initPanel(BB_PANEL_V7_RAW);
    epaper.setPanelSize(EPD_WIDTH, EPD_HEIGHT, BB_PANEL_FLAG_MIRROR_X);
    epaper.setRotation(180);
    // 4 bit per pixel: 16 grays mode
    epaper.setMode(BB_MODE_4BPP);
    int bgcolor = 0xF;
    int fgcolor = 0;
    #if DARKMODE
        bgcolor = 0;
        fgcolor = 0xF;
    #endif

    epaper.fillScreen(bgcolor);
    epaper.setTextColor(fgcolor);
    fb = epaper.currentBuffer();

    /* Initialize NVS. */
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );
    err = nvs_open("storage", NVS_READWRITE, &nvs_h);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    }
    // Read stored
    nvs_get_i16(nvs_h, "boots", &nvs_boots);
    ESP_LOGI(TAG, "-> NVS Boot count: %d", nvs_boots);
    nvs_boots++;
    // Set new value
    nvs_set_i16(nvs_h, "boots", nvs_boots);
    nvs_close(nvs_h);

    // We read sensor here
    scd_read();
   
}

