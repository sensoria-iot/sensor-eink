#include "../config.h"
#include "i2c_bus.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

static void print_scan_once(void)
{
    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");

    for (uint8_t base = 0; base < 0x80; base += 16) {
        printf("%02x: ", base);

        for (uint8_t off = 0; off < 16; off++) {
            uint8_t addr = base + off;

            if (addr < 0x03 || addr > 0x77) {
                printf("   ");
                continue;
            }

            i2c_master_dev_handle_t dev = i2c_bus_get_dev(addr, 100000);
            if (!dev) {
                printf(" --");
                continue;
            }

            uint8_t byte = 0;
            esp_err_t err = i2c_master_receive(dev, &byte, 1, 50);
            if (err != ESP_OK) {
                uint8_t dummy = 0x00;
                err = i2c_master_transmit(dev, &dummy, 1, 50);
            }

            printf((err == ESP_OK) ? " %02x" : " --", addr);
        }
        printf("\n");
    }
    printf("\n");
}

void app_main(void)
{
    
    ESP_ERROR_CHECK(i2c_bus_init(CONFIG_SDA_GPIO, CONFIG_SCL_GPIO));
    // For debugging: show driver errors again (disable silencing for now)
    esp_log_level_set("i2c.master", ESP_LOG_NONE);

    while (1) {
        print_scan_once();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}