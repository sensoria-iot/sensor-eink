// Edit your API setup and general configuration options:
#include "../config.h"
#include "driver/i2c.h"
#define POWER_STATE_PIN   3
#define POWER_HOLD_PIN    21
#define RV3032_INT_PIN    2

// Declare ASCII names for each of the supported RTC types
const char *szType[] = {"Unknown", "PCF8563", "DS3231", "RV3032", "PCF85063A"};
// RTC Alarm
volatile bool rtc_alarm_triggered = false;

extern "C" void IRAM_ATTR rtc_int_isr_handler(void* arg) {
    rtc_alarm_triggered = true;
}

// FastEPD component:
#include "FastEPD.cpp"
FASTEPD epaper;

#define ADC_VOLTAGE_READ
// BQ27426 fuel gauge (For next revision)
//#include "TiFuelGauge.h"
//TiFuelGauge TiFuel;

#ifdef ADC_VOLTAGE_READ
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

    int adc_read_batt() {
        int batt = 0;
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC2_CHANNEL, &adc_raw[0][0]));
        ESP_LOGI("ADC", "ADC%d Channel[%d] Raw Data: %d", ADC_UNIT_1 + 1, ADC2_CHANNEL, adc_raw[0][0]);
        if (do_calibration1_chan0) {
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_chan0_handle, adc_raw[0][0], &voltage[0][0]));
            // TODO: Use different formula for this
            batt = ((adc_raw[0][0]*0.1) - 55) *3;
            ESP_LOGI("ADC", "ADC%d Channel[%d] Cali Voltage: %d mV BATT: %d %%", ADC_UNIT_1 + 1, ADC2_CHANNEL, voltage[0][0], batt);
        }
        return batt;
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
            ESP_LOGI("ADC", "calibration scheme version is %s", "Curve Fitting");
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
            ESP_LOGI("ADC", "calibration scheme version is %s", "Line Fitting");
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
            ESP_LOGI("ADC", "Calibration Success");
        } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
            ESP_LOGW("ADC", "eFuse not burnt, skip software calibration");
        } else {
            ESP_LOGE("ADC", "Invalid arg or no memory");
        }

        return calibrated;
    }

    static void adc_calibration_deinit(adc_cali_handle_t handle)
    {
    #if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        ESP_LOGI("ADC", "deregister %s calibration scheme", "Curve Fitting");
        ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(handle));

    #elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        ESP_LOGI("ADC", "deregister %s calibration scheme", "Line Fitting");
        ESP_ERROR_CHECK(adc_cali_delete_scheme_line_fitting(handle));
    #endif
    }
#endif
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
int res_bienestar_30 = 0;
int res_bienestar_7 = 0;
int res_tendencia_7d = 0;
int res_tendencia_30d = 0;
int res_beneficio_30 = 0;
int res_beneficio_7 = 0;
int res_alert_hrs = 0;
char res_alert_tipo[9];
int res_alert_v = 0;
char res_message[40];

// SCD4x
#include "scd4x_i2c.h"
#include "sensirion_common.h"
#include "sensirion_i2c_hal.h"

// Flag to know that how many times the device booted
int16_t nvs_boots = 0;

#define BUTTON1 GPIO_NUM_46
#define BUTTON2 GPIO_NUM_38
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
        printf("Decoded sleep_minutes: %d\n", sleep_minutes->valueint);
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
    if (rtc_day != cTime.tm_mday) {
        rtc.setTime(&cTime);
        printf("New day, setTime: %02d/%02d/%d %02d:%02d\n", cTime.tm_mday, cTime.tm_mon, cTime.tm_year, cTime.tm_hour, cTime.tm_min);
    
    }
    printf("ALARM: %02d/%02d/%d %02d:%02d\n", aTime.tm_mday, aTime.tm_mon, aTime.tm_year, aTime.tm_hour, aTime.tm_min);
    // has to be ALARM_DAY when changes day
    rtc.setAlarm((rtc_day == aTime.tm_mday) ? ALARM_TIME : ALARM_DAY, &aTime);
}

/**
 * @brief Draws tendencia arrows
 */
void draw_tendencia(int x, int y, int direction) {
    if (direction > 0) {
        epaper.loadG5Image(arrow_up, x, y, 0xF, 0x0);
    } else if (direction < 0) {
        epaper.loadG5Image(arrow_down, x, y, 0xF, 0x0);
    } else {
        epaper.loadG5Image(arrow_neutral, x, y, 0xF, 0x0);
    }
}

/**
 * @brief Draws JSON response in display
 * 
 * @param tipo 
 */
void draw_response_analisis(int tipo) {
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
        snprintf(textbuffer, sizeof(textbuffer), "%d €", res_beneficio_7);
        epaper.drawString(textbuffer, gridx1, gridy1);
        textbuffer[0] = '\0'; // Reset textbuffer to empty string
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
        snprintf(textbuffer, sizeof(textbuffer), "%d%%", res_bienestar_7);
        epaper.drawString(textbuffer, gridx1, gridy1);
        textbuffer[0] = '\0'; // Reset textbuffer to empty string
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
        epaper.drawString("MODEL CONFIDENCE", gridx1, gridy2+50);
        textbuffer[0] = '\0';
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
    } else {
        epaper.drawString("REPORT NEEDS 2 DAYS DATA STREAM", gridx2, gridy2+75);
    }
    epaper.drawString(res_message, gridx1, gridy2+75); // message
    
    epaper.setFont(ubuntu40);
    textbuffer[0] = '\0';
    snprintf(textbuffer, sizeof(textbuffer), "%d%%", res_confianza);
    epaper.drawString(textbuffer, gridx1, gridy2);
    textbuffer[0] = '\0';
    snprintf(textbuffer, sizeof(textbuffer), "%d hrs", res_alert_hrs);
    epaper.drawString(textbuffer, gridx2, gridy2);

    BB_RECT box;
    box.x = 0;
    box.y = 50;
    box.w = EPD_WIDTH;
    box.h = EPD_HEIGHT-230;
    // draw design guides
    epaper.fillRect(30, EPD_HEIGHT/2, EPD_WIDTH-30, 1, 0xA);
    epaper.drawLine(EPD_WIDTH/2, box.y, EPD_WIDTH/2, box.h, 0xA);
    // Tendencia top row arrows
    draw_tendencia(gridx1-100, gridy1-60, res_tendencia_7d);
    draw_tendencia(gridx2-100, gridy1-60, res_tendencia_30d);
    // Confidence & next alert
    epaper.loadG5Image(confidence_chart, gridx1-100, gridy2-60, 0xF, 0x0);
    epaper.loadG5Image(alert, gridx2-100, gridy2-60, 0xF, 0x0);

    epaper.fullUpdate(false, false, &box);
}

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
        ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %" PRId64,
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
    }
    else
    {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    // Correctly showing the JSON response
    ESP_LOG_BUFFER_CHAR(TAG, local_response_buffer, strlen(local_response_buffer));
    parse_json(local_response_buffer);

    // Asign values and then we call new function to draw them
    printf("Sensor tipo:%d alert_tipo:%s confianza:%d bienestar_30:%d bienestar_7:%d\nbeneficio_7:%d mess:%s\n", res_tipo, res_alert_tipo, res_confianza, res_bienestar_30, res_bienestar_7,
    res_beneficio_7, res_message);
    draw_response_analisis(res_tipo);
    
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
    BB_RECT box;
    box.x = 50;
    box.y = 50;
    box.w = 800;
    box.h = 300;
    //epaper.fullUpdate(false, false, box);
    epaper.fillRect(1, y_offset, EPD_WIDTH, 300, 0xF);
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
    epaper.drawString(textbuffer, 450, 110);
    // Reset textbuffer to empty string:
    textbuffer[0] = '\0';
    snprintf(textbuffer, sizeof(textbuffer), "%s", MESSAGE_SCAN_QR2);
    epaper.drawString(textbuffer, 450, 160);

    textbuffer[0] = '\0';
    snprintf(textbuffer, sizeof(textbuffer), "%s", API_KEY);
    epaper.drawString(textbuffer, 458, 310);
    epaper.fullUpdate(false, false, &box);
}

/* Event handler for catching RainMaker events */
static void event_handler_rmk(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == RMAKER_EVENT) {
        switch (event_id) {
            case RMAKER_EVENT_INIT_DONE:
                ESP_LOGI(TAG, "RainMaker Initialised.");
                break;
            case RMAKER_EVENT_CLAIM_STARTED:
                ESP_LOGI(TAG, "RainMaker Claim Started.");
                break;
            case RMAKER_EVENT_CLAIM_SUCCESSFUL:
                ESP_LOGI(TAG, "RainMaker Claim Successful.");
                epaper.fillScreen(16);
                epaper.fullUpdate(false, false);
                break;
            case RMAKER_EVENT_CLAIM_FAILED:
                ESP_LOGI(TAG, "RainMaker Claim Failed.");
                break;
            case RMAKER_EVENT_LOCAL_CTRL_STARTED:
                ESP_LOGI(TAG, "Local Control Started.");
                break;
            case RMAKER_EVENT_LOCAL_CTRL_STOPPED:
                ESP_LOGI(TAG, "Local Control Stopped.");
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
                epaper.drawString("Wi-Fi credentials are cleared", 10, 10);
                epaper.drawString("Will start in WiFi provisioning mode", 10, 30);
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
                ESP_LOGI("NETWORK_EVENT", "Provisioning QR : %s", (char *)event_data);
                esp_qrcode_config_t cfg_qr = ESP_QRCODE_SENSORIA();
                esp_qrcode_generate(&cfg_qr, (const char *)event_data);
                break;
                }
            default:
                ESP_LOGW("NETWORK_EVENT", "Unhandled App Wi-Fi Event: %"PRIi32, event_id);
                break;
        }
    } else if (event_base == RMAKER_OTA_EVENT) {
        switch(event_id) {
            case RMAKER_OTA_EVENT_STARTING:
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
    int x = 100; // EPD_WIDTH/2-300
    int y = 20;
    epaper.drawString(message, x, y);

    epaper.fullUpdate(true, false);
    vTaskDelay(200);
    deep_sleep();
}

void read_batt_level() {
    batt_level = adc_read_batt();
   /* if (TiFuel.is_connected()) {
    batt_level = TiFuel.read_state_of_charge();
    printf("voltage:%d batt_level:%d %%\n\n", TiFuel.read_voltage(), batt_level);
   } */

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
        // IP address.
        esp_netif_ip_info_t ip_info;
        esp_netif_get_ip_info(esp_netif_get_default_netif(), &ip_info);
        sprintf(esp_ip, IPSTR, IP2STR(&ip_info.ip));

        memset(&result, 0, sizeof(json_gen_test_result_t));
        json_gen_str_t jstr;
        json_gen_str_start(&jstr, result.buf, sizeof(result.buf), flush_str, &result);
        json_gen_start_object(&jstr);
        json_gen_obj_set_int(&jstr, "co2", co2);
        json_gen_obj_set_float(&jstr, "temperature", tem);
        json_gen_obj_set_float(&jstr, "humidity", hum);
        json_gen_push_object(&jstr, "client");
        json_gen_obj_set_int(&jstr, "id", JSON_USERID);
        json_gen_obj_set_string(&jstr, "key", API_KEY);
        json_gen_obj_set_string(&jstr, "timezone", JSON_TIMEZONE);
        json_gen_obj_set_string(&jstr, "ip", esp_ip);
        json_gen_obj_set_int(&jstr, "batt_level", batt_level);
        json_gen_end_object(&jstr);
        json_gen_end_object(&jstr);
        json_gen_str_end(&jstr);
        // printf("JSON: %s\n", result.buf);
        

        //epaper.loadG5Image(rainmaker, EPD_WIDTH-320, 50, 0x0, 0xF);
        epaper.fullUpdate(true, false);
    }

    vTaskDelay(pdMS_TO_TICKS(300));
    ESP_LOGI(TAG, "scd4x_power_down()");
    scd4x_power_down();

    sensirion_i2c_hal_free();
}

void app_main()
{
    // IMPORTANT: Set power hold HIGH immediately
    gpio_reset_pin((gpio_num_t)POWER_HOLD_PIN);
    gpio_set_direction((gpio_num_t)POWER_HOLD_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)POWER_HOLD_PIN, 1);
    
    printf("RTC version 1.3\n");

    #ifdef ADC_VOLTAGE_READ
//-------------ADC1 Init---------------//
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    output_buffer = (char*)heap_caps_malloc(MAX_HTTP_OUTPUT_BUFFER, MALLOC_CAP_SPIRAM);
  
    if (output_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
        return;
    }

    //-------------ADC1 Config---------------//
    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC2_CHANNEL, &config));

    //-------------ADC1 Calibration Init---------------//
    do_calibration1_chan0 = adc_calibration_init(ADC_UNIT_1, ADC2_CHANNEL, ADC_ATTEN, &adc1_cali_chan0_handle);

    #endif

    
    // Configure power state pin as input
    gpio_config_t power_conf = {};
    power_conf.mode = GPIO_MODE_INPUT;
    power_conf.pin_bit_mask = (1ULL << POWER_STATE_PIN);
    power_conf.intr_type = GPIO_INTR_DISABLE;
    power_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    power_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&power_conf);
    

    output_buffer = (char*)heap_caps_malloc(MAX_HTTP_OUTPUT_BUFFER, MALLOC_CAP_SPIRAM);
  
    if (output_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
        return;
    }

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

    esp_rmaker_console_init();

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

    printf("Read RTC\n");
    struct tm myTime;
    int rc = rtc.init(CONFIG_SDA_GPIO, CONFIG_SCL_GPIO); // Do not init, already done. CONFIG_SDA_GPIO, CONFIG_SCL_GPIO
    if (rc != RTC_SUCCESS) {
        printf("Error initializing the RTC; stopping...\n");
        while (1) {
            vTaskDelay(1);
        }
    }

    // RTC Clear current alarms and get time
    rtc.clearAlarms();
    rtc.getTime(&myTime);
    rtc_day = myTime.tm_mday;
    printf("%02d:%02d:%02d DAY:%d\n\n", myTime.tm_hour, myTime.tm_min, myTime.tm_sec, myTime.tm_mday);
   
    /* Initialize Wi-Fi/Thread. Note that, this should be called before esp_rmaker_node_init() */
    app_network_init();

    /* Register an event handler to catch RainMaker events */
    ESP_ERROR_CHECK(esp_event_handler_register(RMAKER_COMMON_EVENT, ESP_EVENT_ANY_ID, &event_handler_rmk, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(APP_NETWORK_EVENT, ESP_EVENT_ANY_ID, &event_handler_rmk, NULL));
    /* Initialize the ESP RainMaker Agent.
     * Note that this should be called after app_network_init() but before app_network_start()
     * */
    esp_rmaker_config_t rainmaker_cfg = {
        .enable_time_sync = false,
    };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&rainmaker_cfg, "ESP RainMaker Device", "Lightbulb");
    if (!node)
    {
        ESP_LOGE(TAG, "Could not initialise node. Aborting!!!");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        abort();
    }

    while (!measure_taken) {
        // Waiting for Sensor to report readiness
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    /* Create a device and add the relevant parameters to it */
    // esp_rmaker_device_t *
    temp_sensor_device = esp_rmaker_temp_sensor_device_create("Temperature Sensor", NULL, tem);

    esp_rmaker_device_add_cb(temp_sensor_device, write_cb, NULL);
    // Customized slider to Reset WiFi

    esp_rmaker_param_t *reset_wifi = esp_rmaker_brightness_param_create(DEVICE_PARAM_WIFI_RESET, 0);
    esp_rmaker_param_add_bounds(reset_wifi, esp_rmaker_int(0), esp_rmaker_int(100), esp_rmaker_int(10));
    esp_rmaker_device_add_param(temp_sensor_device, reset_wifi);

    esp_rmaker_node_add_device(node, temp_sensor_device);

    /* Enable OTA */
    esp_rmaker_ota_enable_default();

    /* Enable timezone service which will be require for setting appropriate timezone
     * from the phone apps for scheduling to work correctly.
     * For more information on the various ways of setting timezone, please check
     * https://rainmaker.espressif.com/docs/time-service.html.
     */
    esp_rmaker_timezone_service_enable();

    /* Enable scheduling. */
    esp_rmaker_schedule_enable();

    /* Enable Scenes */
    esp_rmaker_scenes_enable();

    /* Start the ESP RainMaker Agent */
    esp_rmaker_start();

    /* Uncomment to reset WiFi credentials when there is no Boot button in the ESP32 */
    //esp_rmaker_wifi_reset(1,10);return;
    
    
    /* Start the Wi-Fi/Thread.
     * If the node is provisioned, it will start connection attempts,
     * else, it will start Wi-Fi provisioning. The function will return
     * after a connection has been successfully established
     */
    err = app_network_start(POP_TYPE_RANDOM);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not start network. Aborting!!!");
        vTaskDelay(5000/portTICK_PERIOD_MS);
        abort();
    }

    // Initialize SCD40
    //ESP_LOGI(TAG, "CONFIG_SCL_GPIO = %d", CONFIG_SCL_GPIO);
    //ESP_LOGI(TAG, "CONFIG_SDA_GPIO = %d", CONFIG_SDA_GPIO);

    while (!ready_to_measure) {
        // Waiting for WiFi
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    send_data_to_api();

    deep_sleep();
}

