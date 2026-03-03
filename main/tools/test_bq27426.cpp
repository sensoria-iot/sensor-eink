#warning "This is just a TI Fuel gauge test, not the sensoria firmware"
#include "../config.h"
#include "i2c_bus.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "TiFuelGauge.h"

static const char *TAG = "test_bq27426";

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(i2c_bus_init(CONFIG_SDA_GPIO, CONFIG_SCL_GPIO));

    TiFuelGauge TiFuel;

    bool chemistry_set = false;

    while (1) {
        if (TiFuel.is_connected()) {
            if (!chemistry_set) {
                chemistry_set = TiFuel.set_chemistry_profile(TI_CHEM_ID_4_2V);
                ESP_LOGI(TAG, "Chemistry set: %s", chemistry_set ? "OK" : "FAILED");
            }

            uint16_t voltage_mv = TiFuel.read_voltage();
            uint16_t soc = TiFuel.read_state_of_charge();

            ESP_LOGI(TAG, "TI BATT voltage:%u mV batt_level:%u %%", (unsigned)voltage_mv, (unsigned)soc);
        } else {
            ESP_LOGW(TAG, "Fuel gauge not connected (I2C addr 0x%02X)", BQ27426_I2C_ADDRESS);
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}