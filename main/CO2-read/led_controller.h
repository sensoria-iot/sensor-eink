#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include "esp_err.h"
#include "led_strip.h"
#include "freertos/FreeRTOS.h"

// DOTSTAR Led config
#define LED_STRIP_GPIO_PIN  38
// 10MHz resolution, 1 tick = 0.1us (led strip needs a high resolution)
#define LED_STRIP_RMT_RES_HZ  (10 * 1000 * 1000)
#define LED_STRIP_LED_COUNT 1
#define LED_STRIP_MEMORY_BLOCK_WORDS 0

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LED_CMD_SET,
    LED_CMD_CLEAR,
    LED_CMD_BLINK_START,
    LED_CMD_BLINK_STOP
} led_cmd_type_t;

typedef struct {
    led_cmd_type_t type;
    uint8_t r, g, b;
    uint32_t period_ms; // used for BLINK_START
} led_cmd_t;

led_strip_handle_t led_configure(void);
/**
 * @brief Initialize the LED controller task.
 *
 * @param strip Handle returned by led_strip_new_rmt_device (led_strip_handle_t)
 * @param queue_len Queue depth for commands (recommended 1..8). If 0 -> 1
 * @param stack_size Task stack size (bytes). If 0 -> 2048
 * @param prio Task priority. If 0 -> tskIDLE_PRIORITY+1
 *
 * Must be called once after creating the led_strip handle.
 */
esp_err_t led_controller_init(led_strip_handle_t strip, size_t queue_len, uint32_t stack_size, UBaseType_t prio);

/* Non-blocking control API - return true if enqueued (or overwritten for queue_len==1) */
bool led_set_color(uint8_t r, uint8_t g, uint8_t b);
bool led_clear(void);
bool led_blink_start(uint8_t r, uint8_t g, uint8_t b, uint32_t period_ms);
bool led_blink_stop(void);

#ifdef __cplusplus
}
#endif

#endif // LED_CONTROLLER_H