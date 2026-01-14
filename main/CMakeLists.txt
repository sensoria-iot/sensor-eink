# Uncomment only one app_source
idf_build_get_property(target IDF_TARGET)
message(STATUS "→ Building example for ${target}")

# 1 Send SCD40 using Rainmaker
#set(app_sources "CO2-read/station-to-API-fast1.2.cpp") # new AI data

set(app_sources "CO2-read/led_controller.c"
                "CO2-read/STA-RTC-fast-1.4.cpp")
# Only test I2C. RTC address is 0x51
#set(app_sources "tools/i2cscan.c")

# 4 new small white PCB C3 
set(app_requires  
                 fastepd
                 bb_rtc
                 BQ27426_idf
                 json
                 scd40      # sensor
                 qrcode esp_rainmaker  # Wi-FI provision
                 big-fonts  # fonts storage
                 nvs_flash
                 esp_event
                 json_generator
                 esp_wifi
                 esp_http_client
                 esp-tls
                 app_network
                 driver
                 esp_adc
                 )

if (${target} EQUAL "esp32s3")
        list(APPENDS app_requires 
                fastepd)
endif()

idf_component_register(SRCS ${app_sources}
        REQUIRES ${app_requires}
        )
