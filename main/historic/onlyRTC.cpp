// Edit your API setup and general configuration options:
#include "../config.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#define POWER_STATE_PIN   3
#define POWER_HOLD_PIN    21

#define RTC_INT GPIO_NUM_6
#define CONFIG_SDA_GPIO 7
#define CONFIG_SCL_GPIO 15
// Declare ASCII names for each of the supported RTC types
const char *szType[] = {"Unknown", "PCF8563", "DS3231", "RV3032", "PCF85063A"};
// RTC Alarm
volatile bool rtc_alarm_triggered = false;
uint8_t alarm_day = 0;
uint8_t alarm_hour = 0;
uint8_t alarm_min = 0;

extern "C" void IRAM_ATTR rtc_int_isr_handler(void* arg) {
    rtc_alarm_triggered = true;
}

// FastEPD component:
#include "FastEPD.cpp"
FASTEPD epaper;

int batt_level = 0;

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
// Rainmaker
//#include <esp_rmaker_console.h>
#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_params.h>
#include <esp_rmaker_standard_devices.h>
#include <esp_rmaker_schedule.h>
#include <esp_rmaker_console.h>
#include <esp_rmaker_scenes.h>
#include <esp_rmaker_utils.h>
#include <esp_rmaker_ota.h>
#include <esp_rmaker_common_events.h>
#include <app_network.h>
#include <qrcode.h>

#define DARKMODE false

#define ESP_QRCODE_SENSORIA() (esp_qrcode_config_t) { \
    .display_func = esp_qrcode_print_eink, \
    .max_qrcode_version = 10, \
    .qrcode_ecc_level = ESP_QRCODE_ECC_LOW, \
}

bool ready_to_measure = false;
bool measure_taken = false;
float tem = 0;

#define DEVICE_PARAM_WIFI_RESET "Turn slider to 100 to reset WiFi"
#define LOW_BATT_ALERT 20
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

// WIFI
#include "esp_tls.h"
#include "esp_netif.h"
#include "esp_http_client.h"

// RTC
#include <bb_rtc.h>
BBRTC rtc;
uint8_t rtc_day = 0;
//#include <app_insights.h>
esp_rmaker_device_t *temp_sensor_device;

// IP
char esp_ip[16];

// JSON Tools
#include "cJSON.h"
#include <json_generator.h>
typedef struct
{
    char buf[512];
    size_t offset;
} json_gen_test_result_t;
json_gen_test_result_t result;
static char *output_buffer; // Buffer to store response of http request from event handler
// Recollected JSON Values
int res_tipo = 0; // 0=ceo, 1=teams
int res_confianza = 0;
// Issue 12:
const uint8_t color_no_confiable = 0x6;
int res_confiable_bp_semanal = 1; // Por defecto los valores son TRUE en confiable_*
int res_confiable_bp_mensual = 1;
int res_confiable_calidad = 1;
int res_confiable_prediccion = 1;

int res_bienestar_30 = 0;
int res_bienestar_7 = 0;
int res_tendencia_7d = 0;
int res_tendencia_30d = 0;
int res_beneficio_30 = 0;
int res_beneficio_7 = 0;
int res_alert_hrs = 0;
char res_alert_tipo[10];
int res_alert_v = 0;
char res_message[40];

// Flag to know that how many times the device booted
int16_t nvs_boots = 0;

#define BUTTON1 GPIO_NUM_46
bool button1_wakeup = false;
bool button2_wakeup = false;

// EPD framebuffer
uint8_t *fb;

extern "C"
{
    void app_main();
}

void deep_sleep()
{
    printf("5 seconds wait before OFF\n");
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    // TURN ALL OFF
    gpio_set_level((gpio_num_t)POWER_HOLD_PIN, 0);
    ESP_LOGI(pcTaskGetName(0), "DEEP_SLEEP_MINUTES: %d mins to wake-up", nvs_minutes_till_refresh);
    esp_deep_sleep(1000000LL * 60 * nvs_minutes_till_refresh);
}

// Event handler for HTTP requests
esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    static int output_len;      // Stores number of bytes read
    switch (evt->event_id)
    {

    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;

    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;

    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
        break;

    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;

    case HTTP_EVENT_ON_DATA: 
    {
        ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        ESP_LOG_BUFFER_CHAR(TAG, evt->data, evt->data_len);
         if (output_len == 0 && evt->user_data) {
                // we are just starting to copy the output data into the use
                memset(evt->user_data, 0, MAX_HTTP_OUTPUT_BUFFER);
            }
            /*
             *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
             *  However, event handler can also be used in case chunked encoding is used.
             */
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // If user_data buffer is configured, copy the response into the buffer
                int copy_len = 0;
                if (evt->user_data) {
                    // The last byte in evt->user_data is kept for the NULL character in case of out-of-bound access.
                    copy_len = MIN(evt->data_len, (MAX_HTTP_OUTPUT_BUFFER - output_len));
                    if (copy_len) {
                        memcpy(evt->user_data + output_len, evt->data, copy_len);
                    }
                } else {
                    int content_len = esp_http_client_get_content_length(evt->client);
            
                    copy_len = MIN(evt->data_len, (content_len - output_len));
                    if (copy_len) {
                        memcpy(output_buffer + output_len, evt->data, copy_len);
                    }
                }
                output_len += copy_len;
            }

            break;
        }

    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        if (output_buffer != NULL)
        {
            // Response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
            // ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
            free(output_buffer);
            output_buffer = NULL;
        }
        output_len = 0;
        break;

    case HTTP_EVENT_DISCONNECTED:
    {
        ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
        int mbedtls_err = 0;
        esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
        if (err != 0)
        {
            ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
            ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
        }
        if (output_buffer != NULL)
        {
            free(output_buffer);
            output_buffer = NULL;
        }
        output_len = 0;
        break;
    }

    case HTTP_EVENT_REDIRECT:
    {
        ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
        esp_http_client_set_header(evt->client, "Accept", "text/html");
        esp_http_client_set_redirection(evt->client);
        break;
    }
    }
    return ESP_OK;
}

/* Callback to handle commands received from the RainMaker cloud */
static esp_err_t write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
                          const esp_rmaker_param_val_t val, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    if (ctx)
    {
        ESP_LOGI(TAG, "Received write request via : %s", esp_rmaker_device_cb_src_to_str(ctx->src));
    }
    const char *device_name = esp_rmaker_device_get_name(device);
    const char *param_name = esp_rmaker_param_get_name(param);
    if (strcmp(param_name, ESP_RMAKER_DEF_POWER_NAME) == 0)
    {
        ESP_LOGI(TAG, "Received value = %s for %s - %s",
                 val.val.b ? "true" : "false", device_name, param_name);
        if (val.val.b == false)
        {
            deep_sleep();
        }
    }

    else if (strcmp(param_name, DEVICE_PARAM_WIFI_RESET) == 0)
    {
        ESP_LOGI(TAG, "%d for %s-%s",
                 (int)val.val.i, device_name, param_name);
        if (val.val.i == 100)
        {
            printf("Reseting WiFi credentials. Please reprovision your device\n\n");
            esp_rmaker_wifi_reset(1, 10);
        }
    }
    else
    {
        /* Silently ignoring invalid params */
        return ESP_OK;
    }
    esp_rmaker_param_update_and_report(param, val);
    return ESP_OK;
}

void parse_json(const char* json_string)
{
    // Parse the JSON string
    cJSON *root = cJSON_Parse(json_string);
    if (root == NULL) {
        printf("Error before: [%s]\n", cJSON_GetErrorPtr());
        return;
    }

    // sleep_minutes: Get the integer value
    cJSON *sleep_minutes = cJSON_GetObjectItem(root, "sleep_minutes");
    cJSON *sensor_tipo = cJSON_GetObjectItem(root, "tipo");
    cJSON *confianza = cJSON_GetObjectItem(root, "confianza");
    cJSON *confiable_bp_semanal = cJSON_GetObjectItem(root, "confiable_bp_semanal");
    cJSON *confiable_bp_mensual = cJSON_GetObjectItem(root, "confiable_bp_mensual");
    cJSON *confiable_prediccion = cJSON_GetObjectItem(root, "confiable_prediccion");
    cJSON *confiable_calidad = cJSON_GetObjectItem(root, "confiable_calidad");

    cJSON *bienestar_30 = cJSON_GetObjectItem(root, "bienestar_30");
    cJSON *bienestar_7 = cJSON_GetObjectItem(root, "bienestar_7");
    cJSON *tendencia_7 = cJSON_GetObjectItem(root, "tendencia_7d");
    cJSON *tendencia_30 = cJSON_GetObjectItem(root, "tendencia_30d");
    cJSON *beneficio_7 = cJSON_GetObjectItem(root, "beneficio_7");
    cJSON *beneficio_30 = cJSON_GetObjectItem(root, "beneficio_30");
    cJSON *alert_hrs = cJSON_GetObjectItem(root, "alert_hrs");
    cJSON *alert_tipo = cJSON_GetObjectItem(root, "alert_tipo");
    cJSON *alert_v = cJSON_GetObjectItem(root, "alert_v");
    cJSON *message = cJSON_GetObjectItem(root, "message");

    // Parse alarm
    struct tm cTime;
    struct tm aTime;
    cTime.tm_sec = 0;
    aTime.tm_sec = 0;
    cJSON *alarm = cJSON_GetObjectItem(root, "alarm");
    if (alarm) {
        aTime.tm_mday = cJSON_GetObjectItem(alarm, "day")->valueint;
        aTime.tm_mon  = cJSON_GetObjectItem(alarm, "mo")->valueint;
        aTime.tm_year = cJSON_GetObjectItem(alarm, "year")->valueint;
        aTime.tm_hour = cJSON_GetObjectItem(alarm, "hr")->valueint;
        aTime.tm_min = cJSON_GetObjectItem(alarm, "min")->valueint;
        alarm_day = aTime.tm_mday;
        alarm_hour = aTime.tm_hour;
        alarm_min = aTime.tm_min;
    }

    // Parse datetime
    cJSON *datetime = cJSON_GetObjectItem(root, "datetime");
    if (datetime) {
        cTime.tm_wday = cJSON_GetObjectItem(datetime, "wday")->valueint;
        cTime.tm_mday = cJSON_GetObjectItem(datetime, "day")->valueint;
        cTime.tm_mon  = cJSON_GetObjectItem(datetime, "mo")->valueint;
        cTime.tm_year = cJSON_GetObjectItem(datetime, "year")->valueint;
        cTime.tm_hour = cJSON_GetObjectItem(datetime, "hr")->valueint;
        cTime.tm_min = cJSON_GetObjectItem(datetime, "min")->valueint;
    }

    if (cJSON_IsNumber(sleep_minutes)) {
        //printf("Decoded sleep_minutes: %d\n", sleep_minutes->valueint);
        nvs_minutes_till_refresh = sleep_minutes->valueint;
    }
    if (cJSON_IsString(sensor_tipo)) {
        if (strcmp(sensor_tipo->valuestring, "teams") == 0) {
            res_tipo = 1;
        }   
    }
    if (cJSON_IsNumber(confianza)) {
        res_confianza = confianza->valueint;
    }
    // Agrega confiable_*
    if (cJSON_IsNumber(confiable_bp_semanal)) {
        res_confiable_bp_semanal = confiable_bp_semanal->valueint;
    }
    if (cJSON_IsNumber(confiable_bp_mensual)) {
        res_confiable_bp_mensual = confiable_bp_mensual->valueint;
    }
    if (cJSON_IsNumber(confiable_prediccion)) {
        res_confiable_prediccion = confiable_prediccion->valueint;
    }
    if (cJSON_IsNumber(confiable_calidad)) {
        res_confiable_calidad = confiable_calidad->valueint;
    }

    if (cJSON_IsNumber(bienestar_30)) {
        res_bienestar_7 = bienestar_7->valueint;
    }
    if (cJSON_IsNumber(bienestar_30)) {
        res_bienestar_30 = bienestar_30->valueint;
    }
    if (cJSON_IsNumber(tendencia_7)) {
        res_tendencia_7d = tendencia_7->valueint;
    }
    if (cJSON_IsNumber(tendencia_30)) {
        res_tendencia_30d = tendencia_30->valueint;
    }
    if (cJSON_IsNumber(beneficio_7)) {
        res_beneficio_7 = beneficio_7->valueint;
    }
    if (cJSON_IsNumber(beneficio_30)) {
        res_beneficio_30 = beneficio_30->valueint;
    }
    if (cJSON_IsNumber(alert_hrs)) {
        res_alert_hrs = alert_hrs->valueint;
    }
    if (cJSON_IsNumber(alert_v)) {
        res_alert_v = alert_v->valueint;
    }
    if (cJSON_IsString(alert_tipo)) {
        snprintf(res_alert_tipo, strlen(alert_tipo->valuestring), "%s", alert_tipo->valuestring);
    }
    if (cJSON_IsString(message)) {
       snprintf(res_message, strlen(message->valuestring), "%s", message->valuestring);
    }
    // Clean up
    cJSON_Delete(root);

    // Set RTC values
    // TODO: Update time to be set only once per day
    //aTime.tm_hour = 15; //DEBUG
    //aTime.tm_min = 50; //DEBUG
    
    rtc.clearAlarms();
    // For now set this only on DAY 5 of the week
    if (rtc_day != aTime.tm_mday) {
        printf("RTC setTime: %02d/%02d/%d %02d:%02d WDAY:%d\n", cTime.tm_mday, cTime.tm_mon, cTime.tm_year, cTime.tm_hour, cTime.tm_min, cTime.tm_wday);
        rtc.setTime(&cTime); // Set the current time to the RTC
 
        // Use mktime to normalize and manage transitions
        time_t raw_time;
        struct tm normalized_alarm = aTime;

        raw_time = mktime(&normalized_alarm); // Normalize date transitions
        localtime_r(&raw_time, &normalized_alarm); // Apply normalized date

        //rtc.setAlarm(ALARM_DAY, &normalized_alarm); // Set day alarm directly
        rtc.setAlarm(ALARM_TIME, &normalized_alarm); // No more ALARM_DAY let's use always only time alarm in RV3032
        printf("ALARM_DAY: %02d/%02d/%04d %02d:%02d\n", 
        normalized_alarm.tm_mday, normalized_alarm.tm_mon, 
        normalized_alarm.tm_year, normalized_alarm.tm_hour, 
        normalized_alarm.tm_min);
    } else if (nvs_boots < 10) {
        printf("RTC INIT setTime: %02d/%02d/%d %02d:%02d WDAY:%d\n", cTime.tm_mday, cTime.tm_mon, cTime.tm_year, cTime.tm_hour, cTime.tm_min, cTime.tm_wday);
        rtc.setTime(&cTime);
        rtc.setAlarm(ALARM_TIME, &aTime);
    } else {
        // Debug to wake-up every 2 mins:
        //aTime.tm_hour = cTime.tm_hour;
        //aTime.tm_min = cTime.tm_min +4;
        rtc.setAlarm(ALARM_TIME, &aTime); // Same-day alarm
    }
    printf("ALARM_TIME: %02d/%02d/%d %02d:%02d\n", aTime.tm_mday, aTime.tm_mon, aTime.tm_year, aTime.tm_hour, aTime.tm_min);
}

/**
 * @brief Draws tendencia arrows
 */
void draw_tendencia(int x, int y, int direction, uint8_t color = 0x0) {
    if (direction > 0) {
        epaper.loadG5Image(arrow_up, x, y, 0xF, color);
    } else if (direction < 0) {
        epaper.loadG5Image(arrow_down, x, y, 0xF, color);
    } else {
        epaper.loadG5Image(arrow_neutral, x, y, 0xF, color);
    }
}

/**
 * @brief Draws JSON response in display
 * 
 * @param tipo 
 */
void draw_response_analisis(int tipo) {
    epaper.fillRect(0, 80, EPD_WIDTH, 300, 0xF);
    epaper.setFont(ubuntu40);
    int gridx1 = 150; int gridx2 = 800;
    int gridy1 = 200; int gridy2 = 450;
    char textbuffer[40];
    bool ai_data_report = true;
    if (res_beneficio_7 == 0 && res_beneficio_30 == 0 && res_bienestar_7 == 0) {
        ai_data_report = false;
    }
    switch (tipo) {
    case 0: {
        /* ceo */
        epaper.setTextColor((res_confiable_bp_semanal) ? 0x0 : color_no_confiable);
        snprintf(textbuffer, sizeof(textbuffer), "%d €", res_beneficio_7);
        epaper.drawString(textbuffer, gridx1, gridy1);
        textbuffer[0] = '\0'; // Reset textbuffer to empty string
        snprintf(textbuffer, sizeof(textbuffer), "%d%%", res_bienestar_30);
        snprintf(textbuffer, sizeof(textbuffer), "%d €", res_beneficio_30);
        epaper.drawString(textbuffer, gridx2, gridy1);

        epaper.setFont(ubuntu12);
        if (ai_data_report) {
            epaper.drawString("ESTIMATED WEEKLY BENEFIT", gridx1, gridy1+50);
            epaper.drawString("ESTIMATED MONTHLY BENEFIT", gridx2, gridy1+50);
        }
    }
        break;
    
    case 1:
        /* teams */
        epaper.setTextColor((res_confiable_bp_semanal && res_bienestar_7 > 0) ? 0x0 : color_no_confiable);
        snprintf(textbuffer, sizeof(textbuffer), "%d%%", res_bienestar_7);
        epaper.drawString(textbuffer, gridx1, gridy1);
        textbuffer[0] = '\0'; // Reset textbuffer to empty string
        epaper.setTextColor((res_confiable_bp_mensual && res_bienestar_30 > 0) ? 0x0 : color_no_confiable);
        snprintf(textbuffer, sizeof(textbuffer), "%d%%", res_bienestar_30);
        epaper.drawString(textbuffer, gridx2, gridy1);

        epaper.setFont(ubuntu12);
        if (ai_data_report) {
        epaper.drawString("WEEKLY WELLBEING INDEX", gridx1, gridy1+50);
        epaper.drawString("MONTHLY WELLBEING INDEX", gridx2, gridy1+50);
        }
        break;
    }
    // Same for both
    if (ai_data_report) {
        if (res_confiable_bp_semanal == 0) {
                epaper.drawString(not_trustworthy, gridx1, gridy1+90);
            }
            if (res_confiable_bp_mensual == 0) {
                epaper.drawString(not_trustworthy, gridx2, gridy1+90);
            }
        epaper.drawString("MODEL CONFIDENCE", gridx1, gridy2+50);
        textbuffer[0] = '\0';
        if (res_confiable_prediccion) {
            if (strcmp(res_alert_tipo, "NON") == 0) {
                snprintf(textbuffer, sizeof(textbuffer), "NO MORE ALERTS TODAY");
                epaper.drawString(textbuffer, gridx2, gridy2+50);
            }  else {
                snprintf(textbuffer, sizeof(textbuffer), "NEXT PREDICTED %s ALERT:", res_alert_tipo);
                epaper.drawString(textbuffer, gridx2, gridy2+50);
                char * unit_type = "°C";
                if (strcmp(res_alert_tipo, "CO2") == 0) {
                    unit_type = "ppm";
                }
                if (strcmp(res_alert_tipo, "HUM") == 0) {
                    unit_type = "%";
                }
                textbuffer[0] = '\0';
                snprintf(textbuffer, sizeof(textbuffer), "%d %s", res_alert_v, unit_type);
                epaper.drawString(textbuffer, gridx2, gridy2+75);  // Value unit (°C o %)
            }
        } else {
            epaper.setTextColor(color_no_confiable-2);
            snprintf(textbuffer, sizeof(textbuffer), "THE SYSTEM IS STILL LEARNING");
            epaper.drawString(textbuffer, gridx2, gridy2+50);
            textbuffer[0] = '\0';
            snprintf(textbuffer, sizeof(textbuffer), "FROM YOUR DATA");
            epaper.drawString(textbuffer, gridx2, gridy2+80);
        }
    } else {
        epaper.drawString("REPORT NEEDS 2 DAYS DATA STREAM", gridx2, gridy2+75);
    }
    epaper.drawString(res_message, gridx1, gridy2+75); // message
    
    epaper.setFont(ubuntu40);
    epaper.setTextColor((res_confianza>50) ? 0 : color_no_confiable);
    textbuffer[0] = '\0';
    snprintf(textbuffer, sizeof(textbuffer), "%d%%", res_confianza);
    epaper.drawString(textbuffer, gridx1, gridy2);
    
    
    if (res_confiable_prediccion && res_alert_hrs > 0) {
        epaper.setTextColor(0);
        textbuffer[0] = '\0';
        snprintf(textbuffer, sizeof(textbuffer), "%d hrs", res_alert_hrs);
        epaper.drawString(textbuffer, gridx2, gridy2);
    } else {
        epaper.setTextColor(color_no_confiable);
        epaper.drawString("coming soon", gridx2, gridy2);
    }

    BB_RECT box;
    box.x = 0;
    box.y = 40;
    box.w = EPD_WIDTH;
    box.h = EPD_HEIGHT-230;
    // draw design guides
    epaper.fillRect(29, EPD_HEIGHT/2, EPD_WIDTH-30, 1, 0xA);
    epaper.drawLine(EPD_WIDTH/2, box.y, EPD_WIDTH/2, box.h, 0xA);
    // Tendencia top row arrows
    draw_tendencia(gridx1-100, gridy1-60, res_tendencia_7d, (res_confiable_bp_semanal && res_bienestar_7 > 0) ? 0x0 : color_no_confiable);
    draw_tendencia(gridx2-100, gridy1-60, res_tendencia_30d, (res_confiable_bp_mensual && res_bienestar_30 > 0) ? 0x0 : color_no_confiable);
    // Confidence & next alert
    epaper.loadG5Image(confidence_chart, gridx1-100, gridy2-60, 0xF, (res_confianza>50) ? 0x0 : color_no_confiable);
    epaper.loadG5Image(alert, gridx2-100, gridy2-60, 0xF, (res_confiable_prediccion && res_alert_hrs > 0) ? 0x0 : color_no_confiable);

    // NEXT Alarm
    epaper.setFont(ubuntu12);
    epaper.setTextColor(0X0);
    textbuffer[0] = '\0';
    snprintf(textbuffer, sizeof(textbuffer), "NEXT WAKEUP Day:%d %02d:%02d", alarm_day, alarm_hour, alarm_min);
    epaper.drawString(textbuffer, gridx1, 58);

    epaper.fullUpdate(false, false, &box);
}
// --- helper: schedule RTC wake in N minutes (simple normalization) ---
static void schedule_rtc_wakeup_minutes(int minutes)
{
    struct tm now;
    rtc.getTime(&now);

    struct tm alarm = now;
    alarm.tm_min += minutes;

    bool day_changed = false;

    // Normalize minutes -> hours
    while (alarm.tm_min >= 60) {
        alarm.tm_min -= 60;
        alarm.tm_hour += 1;
    }
    // Normalize hours -> day (simple handling; does not fully handle month/year rollovers)
    if (alarm.tm_hour >= 24) {
        alarm.tm_hour -= 24;
        alarm.tm_mday += 1;
        day_changed = true;
    }

    // Update globals used for display
    alarm_day = alarm.tm_mday;
    alarm_hour = alarm.tm_hour;
    alarm_min = alarm.tm_min;

    if (day_changed) {
        rtc.setAlarm(ALARM_DAY, &alarm);
    } else {
        rtc.setAlarm(ALARM_TIME, &alarm);
    }

    // Small delay to ensure RTC writes settle
    vTaskDelay(pdMS_TO_TICKS(200));

    printf("RTC alarm scheduled in %d minutes -> %02d/%02d/%04d %02d:%02d\n",
           minutes, alarm.tm_mday, alarm.tm_mon + 1, alarm.tm_year + 1900, alarm.tm_hour, alarm.tm_min);
}

// Coming back to 1.2 here since it was logging 2 times
void send_data_to_api()
{
    // Declare local_response_buffer with size (MAX_HTTP_OUTPUT_BUFFER + 1) to prevent out of bound access when
    // it is used by functions like strlen(). The buffer should only be used upto size MAX_HTTP_OUTPUT_BUFFER
    char local_response_buffer[MAX_HTTP_OUTPUT_BUFFER + 1] = {0};
    /**
     * NOTE: All the configuration parameters for http_client must be spefied either in URL or as host and path parameters.
     * If host and path parameters are not set, query parameter will be ignored. In such cases,
     * query parameter should be specified in URL.
     *
     * If URL as well as host and path parameters are specified, values of host and path will be considered.
     */
    esp_http_client_config_t config = {
        .url = API_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 3000,
        .event_handler = _http_event_handler,
        .user_data = local_response_buffer, // Pass address of local buffer to get response
    };

    printf("DATA: %s \nURL: %s\n", result.buf, API_URL);
    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, result.buf, strlen(result.buf));
    esp_err_t err = ESP_OK;

    err = esp_http_client_perform(client);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "HTTP POST Status = %d", esp_http_client_get_status_code(client));
    }
    else
    {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    // Got a body - parse and draw
    //ESP_LOG_BUFFER_CHAR(TAG, local_response_buffer, strlen(local_response_buffer));
    parse_json(local_response_buffer);
    draw_response_analisis(res_tipo);

    // Clean up
    esp_http_client_cleanup(client);

}

static void flush_str(char *buf, void *priv)
{
    printf("flush_str buf=[%s]\r\n", buf);
    json_gen_test_result_t *result = (json_gen_test_result_t *)priv;
    if (result)
    {
        if (strlen(buf) > sizeof(result->buf) - result->offset)
        {
            printf("Result Buffer too small\r\n");
            return;
        }
        memcpy(result->buf + result->offset, buf, strlen(buf));
        result->offset += strlen(buf);
    }
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

// esp_qrcode_config_t
void esp_qrcode_print_eink(esp_qrcode_handle_t qrcode) {
    int size = esp_qrcode_get_size(qrcode);
    int sq = 5;
    uint8_t color;
    int x_offset = EPD_WIDTH -260;
    int y_offset = 90;
    int border = 2;

    epaper.fillRect(0, y_offset, EPD_WIDTH, 300, 0xF);
    for (int y = -2; y < size + border; y ++) {
        for (int x = -2; x < size + border; x ++) {
            color = esp_qrcode_get_module(qrcode, x, y);
            if (color) { color = 0xF; }
            
            epaper.fillRect((x*sq)+x_offset, (y*sq)+y_offset, sq, sq, color);
            }
    }
    
    epaper.setFont(ubuntu20);
    char textbuffer[40];
    snprintf(textbuffer, sizeof(textbuffer), "%s", MESSAGE_SCAN_QR1);
    epaper.drawString(textbuffer, 430, 110);
    // Reset textbuffer to empty string:
    textbuffer[0] = '\0';
    snprintf(textbuffer, sizeof(textbuffer), "%s", MESSAGE_SCAN_QR2);
    epaper.drawString(textbuffer, 430, 160);

    textbuffer[0] = '\0';
    snprintf(textbuffer, sizeof(textbuffer), "%s", SENSOR_ID);
    epaper.drawString(textbuffer, 462, 310);

    textbuffer[0] = '\0';
    snprintf(textbuffer, sizeof(textbuffer), "%s welcomes you!", WEB_HOST);
    epaper.drawString(textbuffer, 462, 360);
    
    BB_RECT box;
    box.x = 50;
    box.y = 50;
    box.w = 800;
    box.h = 360;
    epaper.fullUpdate(false, false, &box);
}

/* Event handler for catching RainMaker events */
static void event_handler_rmk(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == RMAKER_EVENT) {
        switch (event_id) {
            case RMAKER_EVENT_INIT_DONE:
                ESP_LOGI(TAG, "EVENT RainMaker Initialised.");
                break;
            case RMAKER_EVENT_CLAIM_STARTED:
                //led_blink_start(0, 0, 50, 500);
                ESP_LOGI(TAG, "EVENT RainMaker Claim Started.");
                epaper.fillScreen(16);
                epaper.fullUpdate(CLEAR_FAST, false);
                vTaskDelay(pdMS_TO_TICKS(300));
                break;
            case RMAKER_EVENT_CLAIM_SUCCESSFUL:
                ESP_LOGI(TAG, "EVENT RainMaker Claim Successful.");
                epaper.fillScreen(16);
                epaper.fullUpdate(CLEAR_FAST, false);
                vTaskDelay(pdMS_TO_TICKS(300));
                break;
            case RMAKER_EVENT_USER_NODE_MAPPING_DONE:
                //led_blink_start(50, 0, 0, 500);
                ESP_LOGI(TAG, "EVENT RainMaker Claim Failed.");
                break;
            case RMAKER_EVENT_CLAIM_FAILED:
                //led_blink_start(50, 0, 0, 500);
                ESP_LOGI(TAG, "EVENT RainMaker Claim Failed.");
                break;
            case RMAKER_EVENT_LOCAL_CTRL_STARTED:
                ESP_LOGI(TAG, "EVENT Local Control Started.");
                break;
            case RMAKER_EVENT_LOCAL_CTRL_STOPPED:
                ESP_LOGI(TAG, "EVENT Local Control Stopped.");
                break;
            default:
                ESP_LOGW(TAG, "Unhandled RainMaker Event: %"PRIi32, event_id);
        }
    } else if (event_base == RMAKER_COMMON_EVENT) {
        switch (event_id) {
            case RMAKER_EVENT_REBOOT:
                ESP_LOGI(TAG, "Rebooting in %d seconds.", *((uint8_t *)event_data));
                break;
            case RMAKER_EVENT_WIFI_RESET:
                ESP_LOGI(TAG, "Wi-Fi credentials reset.");
                epaper.drawString("Wi-Fi credentials are cleared", 10, 45);
                epaper.fullUpdate();
                break;
            case RMAKER_EVENT_FACTORY_RESET:
                ESP_LOGI(TAG, "Node reset to factory defaults.");
                break;
            case RMAKER_MQTT_EVENT_CONNECTED:
                ESP_LOGI(TAG, "MQTT Connected.");
                break;
            case RMAKER_MQTT_EVENT_DISCONNECTED:
                ESP_LOGI(TAG, "MQTT Disconnected.");
                break;
            case RMAKER_MQTT_EVENT_PUBLISHED: 
                ESP_LOGI(TAG, "MQTT Published. Msg id: %d.", *((int *)event_data));
                // Ready to do something?
                ready_to_measure = true;
                break;
                
            default:
                ESP_LOGW(TAG, "Unhandled RainMaker Common Event: %"PRIi32, event_id);
        }
    } else if (event_base == APP_NETWORK_EVENT) {
        switch (event_id) {
            case APP_NETWORK_EVENT_QR_DISPLAY: {
                //led_blink_start(0, 0, 50, 500);
                ESP_LOGI("NETWORK_EVENT", "Provisioning QR : %s", (char *)event_data);
                esp_qrcode_config_t cfg_qr = ESP_QRCODE_SENSORIA();
                esp_qrcode_generate(&cfg_qr, (const char *)event_data);
                break;
                }
            case APP_NETWORK_EVENT_PROV_TIMEOUT: {
                 //led_blink_start(50, 0, 0, 500);
                 ESP_LOGI("NETWORK_EVENT", "Provisioning timed-out");
                 epaper.fillRect(0, 80, EPD_WIDTH, 400, 0x0);
                 epaper.fillRect(0, 80, EPD_WIDTH, 400, 0xF);
                 epaper.drawString("Provisioning timed-out.", 10, 110);
                 epaper.drawString("Press wake for half a second or connect your device to USB-C", 10, 160);
                 epaper.drawString("The LED signal should flash BLUE when it's ready", 10, 210);
                 epaper.fullUpdate();
            }
            default:
                ESP_LOGW("NETWORK_EVENT", "Unhandled App Wi-Fi Event: %"PRIi32, event_id);
                break;
        }
    } else if (event_base == RMAKER_OTA_EVENT) {
        switch(event_id) {
            case RMAKER_OTA_EVENT_STARTING:
                //led_blink_start(0, 50, 0, 500);
                ESP_LOGI(TAG, "Starting OTA.");
                break;
            case RMAKER_OTA_EVENT_IN_PROGRESS:
                ESP_LOGI(TAG, "OTA is in progress.");
                break;
            case RMAKER_OTA_EVENT_SUCCESSFUL:
                ESP_LOGI(TAG, "OTA successful.");
                break;
            case RMAKER_OTA_EVENT_FAILED:
                ESP_LOGI(TAG, "OTA Failed.");
                break;
            case RMAKER_OTA_EVENT_REJECTED:
                ESP_LOGI(TAG, "OTA Rejected.");
                break;
            case RMAKER_OTA_EVENT_DELAYED:
                ESP_LOGI(TAG, "OTA Delayed.");
                break;
            case RMAKER_OTA_EVENT_REQ_FOR_REBOOT:
                ESP_LOGI(TAG, "Firmware image downloaded. Please reboot your device to apply the upgrade.");
                break;
            default:
                ESP_LOGW(TAG, "Unhandled OTA Event: %"PRIi32, event_id);
                break;
        }
    } else {
        ESP_LOGW(TAG, "Invalid event received!");
    }
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
    epaper.setFont(ubuntu30);
    char textbuffer[20];
    snprintf(textbuffer, sizeof(textbuffer), "%d", co2);
    epaper.drawString(textbuffer, x, y);

    epaper.setFont(ubuntu20);
    snprintf(textbuffer, sizeof(textbuffer), "ppm");
    x += (co2 < 1000) ? 130 : 160;
    epaper.drawString(textbuffer, x, y);
    epaper.setFont(ubuntu30);
    
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
    //led_blink_start(50, 0, 0, 500);
    int x = 100; // EPD_WIDTH/2-300
    int y = 20;
    epaper.drawString(message, x, y);

    epaper.fullUpdate(true, false);
    vTaskDelay(pdMS_TO_TICKS(1000));
    deep_sleep();
}


void app_main()
{

    printf("RTC version only 1.4\n");
    
gpio_set_direction(RTC_INT, GPIO_MODE_INPUT);
    /* Initialize NVS. */
   esp_err_t err = nvs_flash_init();
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

    printf("Read RTC\n");
    struct tm myTime;
    int rc = rtc.init(CONFIG_SDA_GPIO, CONFIG_SCL_GPIO); // Do not init, already done. CONFIG_SDA_GPIO, CONFIG_SCL_GPIO
    if (rc != RTC_SUCCESS) {
        printf("Error in rtc.init() I2C is already initialized\n");
        /* while (1) {
            vTaskDelay(1);
        } */
    } else {
        rtc.clearAlarms();
        // RTC get time
        rtc.getTime(&myTime);
        rtc_day = myTime.tm_mday;
        printf("%02d:%02d:%02d DAY:%d MO:%d WDAY:%d\n\n", myTime.tm_hour, myTime.tm_min, myTime.tm_sec, myTime.tm_mday, myTime.tm_mon, myTime.tm_wday);
    }
    bool setTime = false;
    if (setTime) {
        myTime.tm_hour = 17;
        myTime.tm_min = 30;
        myTime.tm_sec = 0;
        myTime.tm_mday = 15;
        myTime.tm_mon = 3;
        myTime.tm_wday = 6;
        myTime.tm_year = 2026;
        rtc.setTime(&myTime);
    }

    struct tm aTime;
    myTime.tm_wday = 6;
    aTime.tm_mday = 15;
    aTime.tm_mon = 3;
    aTime.tm_year = 2026;
    aTime.tm_hour = 17;
    aTime.tm_min = 47;
aTime.tm_sec=0;
    rtc.setAlarm(ALARM2_TIME, &aTime);
    printf("ALARM_TIME: %02d/%02d/%d %02d:%02d\n", aTime.tm_mday, aTime.tm_mon, aTime.tm_year, aTime.tm_hour, aTime.tm_min);

    while (gpio_get_level(RTC_INT)) {
        vTaskDelay(10);
        printf("RTC_INT high\n");
    }
    printf("RTC_INT LOW: Alarm\n");
}

