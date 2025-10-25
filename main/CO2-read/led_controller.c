#include "led_controller.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "led_strip.h"
#include <string.h>

static const char *TAG = "LED_CTRL";

static QueueHandle_t s_queue = NULL;
static TaskHandle_t s_task = NULL;
static led_strip_handle_t s_strip = NULL;

led_strip_handle_t led_configure(void)
{
    // LED strip general initialization, according to your led board design
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO_PIN, // The GPIO that connected to the LED strip's data line
        .max_leds = LED_STRIP_LED_COUNT,      // The number of LEDs in the strip,
        .led_model = LED_MODEL_WS2812,        // LED strip model
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB, // The color order of the strip: GRB
        .flags = {
            .invert_out = false, // don't invert the output signal
        }
    };
    // LED strip backend configuration: RMT
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,        // different clock source can lead to different power consumption
        .resolution_hz = LED_STRIP_RMT_RES_HZ, // RMT counter clock frequency
        .mem_block_symbols = LED_STRIP_MEMORY_BLOCK_WORDS, // the memory block size used by the RMT channel
        .flags = {
            .with_dma = 0
        }
    };
    // LED Strip object handle
    led_strip_handle_t led_strip;
    // LED Strip object handle
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    return led_strip;
}

/* Minimal helpers */
static void led_apply_color(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_strip) return;
    esp_err_t err = led_strip_set_pixel(s_strip, 0, r, g, b); // pixel 0 for single LED
    if (err == ESP_OK) err = led_strip_refresh(s_strip);
    if (err != ESP_OK) ESP_LOGW(TAG, "apply_color fail: %s", esp_err_to_name(err));
}

static void led_apply_clear(void)
{
    if (!s_strip) return;
    esp_err_t err = led_strip_clear(s_strip);
    if (err != ESP_OK) ESP_LOGW(TAG, "apply_clear fail: %s", esp_err_to_name(err));
}

/* Task: intentionally tiny */
static void led_task(void *arg)
{
    (void)arg;
    led_cmd_t cmd;
    bool blinking = false;
    uint32_t half_ticks = 0;
    TickType_t last = 0;
    bool on = false;
    uint8_t br = 0, bg = 0, bb = 0;

    led_apply_clear();

    for (;;) {
        TickType_t wait = blinking ? half_ticks : portMAX_DELAY;
        if (xQueueReceive(s_queue, &cmd, wait)) {
            switch (cmd.type) {
            case LED_CMD_SET:
                blinking = false;
                led_apply_color(cmd.r, cmd.g, cmd.b);
                break;
            case LED_CMD_CLEAR:
                blinking = false;
                led_apply_clear();
                break;
            case LED_CMD_BLINK_START:
                blinking = true;
                br = cmd.r; bg = cmd.g; bb = cmd.b;
                if (cmd.period_ms < 2) cmd.period_ms = 500;
                half_ticks = pdMS_TO_TICKS(cmd.period_ms / 2) ? pdMS_TO_TICKS(cmd.period_ms / 2) : 1;
                on = true;
                led_apply_color(br, bg, bb);
                last = xTaskGetTickCount();
                break;
            case LED_CMD_BLINK_STOP:
                blinking = false;
                led_apply_clear();
                break;
            default:
                break;
            }
        } else {
            /* timeout -> toggle when blinking */
            if (blinking) {
                on = !on;
                if (on) led_apply_color(br, bg, bb);
                else led_apply_clear();
                last = xTaskGetTickCount();
            }
        }
    }
}

/* Public API */
esp_err_t led_controller_init(led_strip_handle_t strip, size_t queue_len, uint32_t stack_size, UBaseType_t prio)
{
    if (!strip) return ESP_ERR_INVALID_ARG;
    if (s_task) return ESP_ERR_INVALID_STATE; // already init

    s_strip = strip;
    size_t qlen = queue_len ? queue_len : 1;
    s_queue = xQueueCreate(qlen, sizeof(led_cmd_t));
    if (!s_queue) {
        ESP_LOGE(TAG, "xQueueCreate failed");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t rc = xTaskCreate(led_task, "led_task", stack_size ? stack_size : 2048,
                                NULL, prio ? prio : tskIDLE_PRIORITY+1, &s_task);
    if (rc != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        ESP_LOGE(TAG, "xTaskCreate failed");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

/* Use xQueueOverwrite when queue length == 1 for latest-command semantics,
   otherwise use xQueueSend with zero wait (non-blocking). */
static bool enqueue_cmd(const led_cmd_t *cmd)
{
    if (!s_queue) return false;

    UBaseType_t uxMessagesWaiting = uxQueueMessagesWaiting(s_queue);
    UBaseType_t qs = uxQueueSpacesAvailable(s_queue);

    if (uxQueueSpacesAvailable(s_queue) == 0 && uxQueueMessagesWaiting(s_queue) == 1) {
        // queue full and depth==1 -> overwrite
        return xQueueOverwrite(s_queue, cmd) == pdPASS;
    } else {
        return xQueueSend(s_queue, cmd, 0) == pdPASS;
    }
}

bool led_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    led_cmd_t cmd = { .type = LED_CMD_SET, .r = r, .g = g, .b = b, .period_ms = 0 };
    return enqueue_cmd(&cmd);
}

bool led_clear(void)
{
    led_cmd_t cmd = { .type = LED_CMD_CLEAR };
    return enqueue_cmd(&cmd);
}

bool led_blink_start(uint8_t r, uint8_t g, uint8_t b, uint32_t period_ms)
{
    led_cmd_t cmd = { .type = LED_CMD_BLINK_START, .r = r, .g = g, .b = b, .period_ms = period_ms };
    return enqueue_cmd(&cmd);
}

bool led_blink_stop(void)
{
    led_cmd_t cmd = { .type = LED_CMD_BLINK_STOP };
    return enqueue_cmd(&cmd);
}