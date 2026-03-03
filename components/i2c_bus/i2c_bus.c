#include "i2c_bus.h"
#include "esp_check.h"
#include <string.h>

static const char *TAG = "i2c_bus";

static i2c_master_bus_handle_t s_bus = NULL;

typedef struct {
    uint8_t addr;
    uint32_t speed_hz;
    i2c_master_dev_handle_t dev;
} dev_entry_t;

static dev_entry_t s_devs[128];
static int s_dev_count = 0;

esp_err_t i2c_bus_init(int sda_gpio, int scl_gpio)
{
    if (s_bus) return ESP_OK;

    i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = (gpio_num_t)sda_gpio,
        .scl_io_num = (gpio_num_t)scl_gpio,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = 1,
            .allow_pd = 0,
        },
    };

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&cfg, &s_bus), TAG, "i2c_new_master_bus failed");
    s_dev_count = 0;
    memset(s_devs, 0, sizeof(s_devs));
    return ESP_OK;
}

i2c_master_dev_handle_t i2c_bus_get_dev(uint8_t addr7, uint32_t speed_hz)
{
    if (!s_bus) return NULL;

    for (int i = 0; i < s_dev_count; i++) {
        if (s_devs[i].addr == addr7 && s_devs[i].speed_hz == speed_hz) {
            return s_devs[i].dev;
        }
    }
    if (s_dev_count >= (int)(sizeof(s_devs)/sizeof(s_devs[0]))) return NULL;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr7,
        .scl_speed_hz = speed_hz,
    };

    i2c_master_dev_handle_t dev = NULL;
    if (i2c_master_bus_add_device(s_bus, &dev_cfg, &dev) != ESP_OK) return NULL;

    s_devs[s_dev_count++] = (dev_entry_t){ .addr = addr7, .speed_hz = speed_hz, .dev = dev };
    return dev;
}